// Define this before any includes for POSIX functions like strdup
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "irc_bot.h"
#include <string.h> 

// --- Versatile Logging Function (Dual Output) ---
void app_log(const char *process_tag, const char *level, const char *format, ...) {
    char message_buffer[MAX_PIPE_MSG_LEN + 256]; // Buffer for the formatted message
    va_list args_for_sprintf;
    va_start(args_for_sprintf, format);
    vsnprintf(message_buffer, sizeof(message_buffer), format, args_for_sprintf);
    va_end(args_for_sprintf);

    time_t now;
    struct tm *local_time_info;
    char time_buffer[80];
    time(&now);
    local_time_info = localtime(&now);
    if (local_time_info != NULL) {
        strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", local_time_info);
    } else {
        strcpy(time_buffer, "NO_TIMESTAMP");
    }

    // Print to console (stdout)
    fprintf(stdout, "[%s] [%s] [%s] %s\n", time_buffer, process_tag, level, message_buffer);
    fflush(stdout); // Ensure console output is immediate

    // Print to log file
    FILE *logFile = fopen(LOG_FILE_PATH, "a");
    if (logFile == NULL) {
        fprintf(stderr, "[%s] [%s] [CRITICAL_LOG_ERROR] Failed to open log file '%s': %s. Original message: %s\n", 
                time_buffer, process_tag, LOG_FILE_PATH, strerror(errno), message_buffer);
        fflush(stderr);
        return;
    }
    fprintf(logFile, "[%s] [%s] [%s] %s\n", time_buffer, process_tag, level, message_buffer);
    fflush(logFile); 
    fclose(logFile);
}


void clearBuffer(char buffer[], size_t size) {
    memset(buffer, 0, size);
}

bool is_other_bot_nick(const char *nick) {
    if (nick == NULL) return false;
    size_t len = strlen(nick);
    if (len != 9) return false;
    if (nick[0] != 'b') return false; 
    for (int i = 1; i <= 4; ++i) if (!isalpha(nick[i])) return false;
    for (int i = 5; i <= 8; ++i) if (!isdigit(nick[i])) return false;
    return true;
}

int load_channels_from_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    char parent_tag[32]; // For logging within this function
    snprintf(parent_tag, sizeof(parent_tag), "Parent %d", getpid());

    if (file == NULL) {
        app_log(parent_tag, "ERROR", "Error opening channels file '%s': %s", filename, strerror(errno));
        return -1;
    }

    char line[MAX_CHANNEL_NAME_LEN + MAX_PERSONA_LEN + 5]; // Room for channel, ';', persona, \n, \0
    int count = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = 0; // Remove newline
        if (strlen(line) > 0 && line[0] == '#') { // Basic validation for a channel line
            char* semicolon = strchr(line, ';');
            if (semicolon != NULL && (semicolon - line) < MAX_CHANNEL_NAME_LEN) { // Check if channel name part is valid length
                 count++;
            } else if (semicolon == NULL && strlen(line) < MAX_CHANNEL_NAME_LEN) { // No semicolon, just channel name
                 count++;
            }
        }
    }

    if (count == 0) {
        app_log(parent_tag, "WARN", "No valid channels found in %s", filename);
        fclose(file);
        return -1; 
    }

    if (g_channel_infos != NULL) free_channels_memory(); // Free existing if any

    g_channel_infos = (ChannelInfo *)malloc(count * sizeof(ChannelInfo)); // No +1 for NULL here, count is exact
    if (g_channel_infos == NULL) {
        app_log(parent_tag, "ERROR", "Failed to allocate memory for ChannelInfo array: %s", strerror(errno));
        fclose(file);
        return -1;
    }
    // Initialize to NULL to prevent issues if strdup fails later
    for(int k=0; k<count; ++k) {
        g_channel_infos[k].name = NULL;
        g_channel_infos[k].persona = NULL;
    }


    rewind(file);
    int current_channel_idx = 0;
    while (fgets(line, sizeof(line), file) != NULL && current_channel_idx < count) {
        line[strcspn(line, "\r\n")] = 0; 
        if (strlen(line) == 0 || line[0] != '#') continue;

        char *channel_part = line;
        char *persona_part = NULL;
        char *semicolon = strchr(line, ';');

        if (semicolon != NULL) {
            *semicolon = '\0'; // Null-terminate channel part
            persona_part = semicolon + 1;
            // Validate lengths
            if (strlen(channel_part) >= MAX_CHANNEL_NAME_LEN || strlen(persona_part) >= MAX_PERSONA_LEN) {
                app_log(parent_tag, "WARN", "Skipping line due to length: %s", line); // Log original line before modification
                *semicolon = ';'; // Restore for logging if needed elsewhere
                continue;
            }
        } else {
            if (strlen(channel_part) >= MAX_CHANNEL_NAME_LEN) {
                 app_log(parent_tag, "WARN", "Skipping channel due to name length: %s", channel_part);
                 continue;
            }
            // No persona specified, will use default later or handle as NULL
        }
        
        g_channel_infos[current_channel_idx].name = strdup(channel_part);
        if (g_channel_infos[current_channel_idx].name == NULL) {
            app_log(parent_tag, "ERROR", "Failed to duplicate channel name: %s", strerror(errno));
            // Cleanup partially filled g_channel_infos
            for(int k=0; k <= current_channel_idx; ++k) {
                free(g_channel_infos[k].name); free(g_channel_infos[k].persona);
            }
            free(g_channel_infos); g_channel_infos = NULL;
            fclose(file);
            return -1;
        }

        if (persona_part && strlen(persona_part) > 0) {
            g_channel_infos[current_channel_idx].persona = strdup(persona_part);
            if (g_channel_infos[current_channel_idx].persona == NULL) {
                app_log(parent_tag, "ERROR", "Failed to duplicate persona string: %s", strerror(errno));
                free(g_channel_infos[current_channel_idx].name);
                // Cleanup partially filled g_channel_infos
                for(int k=0; k < current_channel_idx; ++k) { // Note: k < current_channel_idx
                    free(g_channel_infos[k].name); free(g_channel_infos[k].persona);
                }
                free(g_channel_infos); g_channel_infos = NULL;
                fclose(file);
                return -1;
            }
        } else {
            g_channel_infos[current_channel_idx].persona = strdup("You are a helpful IRC bot assistant."); // Default persona
             if (g_channel_infos[current_channel_idx].persona == NULL) {
                 app_log(parent_tag, "ERROR", "Failed to duplicate default persona string: %s", strerror(errno));
                 // Handle error similar to above
             }
        }
        current_channel_idx++;
    }
    fclose(file);

    if (current_channel_idx > 0 && g_channel_infos[0].name != NULL) { 
        ADMIN_CHANNEL_NAME_CONST = g_channel_infos[0].name;
    } else {
        ADMIN_CHANNEL_NAME_CONST = NULL;
    }

    numChildren = current_channel_idx; // numChildren is now the actual count of loaded channels
    numWorkerChildren = (numChildren > 0 && ADMIN_CHANNEL_NAME_CONST != NULL) ? (numChildren - 1) : numChildren;


    app_log(parent_tag, "INFO", "Loaded %d channels. Admin channel: %s. Worker channels: %d",
           numChildren, ADMIN_CHANNEL_NAME_CONST ? ADMIN_CHANNEL_NAME_CONST : "N/A", numWorkerChildren);
    return numChildren;
}

void free_channels_memory(void) {
    char parent_tag[32];
    snprintf(parent_tag, sizeof(parent_tag), "Parent %d", getpid());
    if (g_channel_infos != NULL) {
        for (int i = 0; i < numChildren; i++) { // Iterate numChildren times
            free(g_channel_infos[i].name);
            free(g_channel_infos[i].persona);
        }
        free(g_channel_infos);
        g_channel_infos = NULL;
    }
    numChildren = 0;
    numWorkerChildren = 0;
    ADMIN_CHANNEL_NAME_CONST = NULL; 
    app_log(parent_tag, "INFO", "Freed channels memory.");
}

int load_muted_users_from_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        app_log("Setup", "INFO", "Muted users file '%s' not found or not readable. No users pre-muted.", filename);
        g_num_muted_users = 0; 
        g_muted_nicks = NULL;  
        return 0; 
    }

    char line[MAX_NICK_LEN + 2]; 
    int count = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = 0;
        if (strlen(line) > 0 && strlen(line) < MAX_NICK_LEN) { 
            count++;
        }
    }
    
    if (g_muted_nicks != NULL) free_muted_users_memory();

    if (count == 0) {
        fclose(file);
        g_num_muted_users = 0;
        g_muted_nicks = NULL;
        app_log("Setup", "INFO", "No nicks found in muted users file %s.", filename);
        return 0;
    }

    g_muted_nicks = (char **)malloc((count + 1) * sizeof(char *)); 
    if (g_muted_nicks == NULL) {
        app_log("Setup", "ERROR", "Failed to allocate memory for muted nicks array: %s", strerror(errno));
        fclose(file);
        return -1; 
    }

    rewind(file);
    int current_idx = 0;
    while (fgets(line, sizeof(line), file) != NULL && current_idx < count) {
        line[strcspn(line, "\r\n")] = 0;
        if (strlen(line) > 0 && strlen(line) < MAX_NICK_LEN) {
            g_muted_nicks[current_idx] = strdup(line);
            if (g_muted_nicks[current_idx] == NULL) {
                app_log("Setup", "ERROR", "Failed to duplicate muted nick string: %s", strerror(errno));
                for (int i = 0; i < current_idx; i++) free(g_muted_nicks[i]);
                free(g_muted_nicks); g_muted_nicks = NULL;
                fclose(file);
                return -1; 
            }
            current_idx++;
        }
    }
    g_muted_nicks[count] = NULL; 
    fclose(file);

    g_num_muted_users = count;
    app_log("Setup", "INFO", "Loaded %d muted user(s) from %s.", g_num_muted_users, filename);
    return count;
}

void free_muted_users_memory(void) {
    if (g_muted_nicks != NULL) {
        for (int i = 0; g_muted_nicks[i] != NULL; i++) { 
            free(g_muted_nicks[i]);
        }
        free(g_muted_nicks);
        g_muted_nicks = NULL;
    }
    g_num_muted_users = 0;
    app_log("Shutdown", "INFO", "Freed muted users memory.");
}

int save_muted_users_to_file(const char *filename) {
    char proc_tag[32]; // Generic tag, could be improved if context is available
    snprintf(proc_tag, sizeof(proc_tag), "PID %d", getpid());

    FILE *file = fopen(filename, "w"); 
    if (file == NULL) {
        app_log(proc_tag, "ERROR", "Error opening muted users file '%s' for writing: %s", filename, strerror(errno));
        return -1;
    }
    if (g_muted_nicks != NULL) {
        for (int i = 0; g_muted_nicks[i] != NULL; i++) { 
            if (fprintf(file, "%s\n", g_muted_nicks[i]) < 0) {
                app_log(proc_tag, "ERROR", "Error writing to muted users file '%s': %s", filename, strerror(errno));
                fclose(file);
                return -1;
            }
        }
    }
    fclose(file);
    app_log(proc_tag, "INFO", "Muted users list saved to %s.", filename);
    return 0;
}
// ... (ensure other mute functions also use app_log similarly) ...
// (load_muted_users_from_file, free_muted_users_memory, add_muted_user, remove_muted_user 
// are already updated in the previous turn to use app_log - verify and keep those changes)
bool is_user_globally_muted(const char *nick) {
    if (nick == NULL || g_muted_nicks == NULL) {
        return false;
    }
    for (int i = 0; g_muted_nicks[i] != NULL; i++) { 
        if (strcmp(nick, g_muted_nicks[i]) == 0) {
            return true;
        }
    }
    return false;
}

int add_muted_user(const char *nick) {
    char proc_tag[32];
    snprintf(proc_tag, sizeof(proc_tag), "PID %d", getpid());
    if (nick == NULL || strlen(nick) == 0 || strlen(nick) >= MAX_NICK_LEN) {
        app_log(proc_tag, "WARN", "Invalid nick '%s' provided for muting.", nick ? nick : "NULL");
        return -1; 
    }
    if (is_user_globally_muted(nick)) {
        app_log(proc_tag, "INFO", "User '%s' is already muted.", nick);
        return 0; 
    }

    char **new_muted_nicks = (char **)realloc(g_muted_nicks, (g_num_muted_users + 2) * sizeof(char *));
    if (new_muted_nicks == NULL) {
        app_log(proc_tag, "ERROR", "Failed to realloc memory for muted nicks array: %s", strerror(errno));
        return -1; 
    }
    g_muted_nicks = new_muted_nicks;

    g_muted_nicks[g_num_muted_users] = strdup(nick);
    if (g_muted_nicks[g_num_muted_users] == NULL) {
        app_log(proc_tag, "ERROR", "Failed to duplicate nick string for muting: %s", strerror(errno));
        g_muted_nicks = (char **)realloc(g_muted_nicks, (g_num_muted_users + 1) * sizeof(char *));
        if(g_muted_nicks) g_muted_nicks[g_num_muted_users] = NULL; 
        return -1; 
    }

    g_num_muted_users++;
    g_muted_nicks[g_num_muted_users] = NULL; 

    app_log(proc_tag, "MUTE_ADD", "User '%s' added to mute list. Total muted: %d.", nick, g_num_muted_users);
    return save_muted_users_to_file(MUTED_USERS_FILE_PATH);
}

int remove_muted_user(const char *nick) {
    char proc_tag[32];
    snprintf(proc_tag, sizeof(proc_tag), "PID %d", getpid());
    if (nick == NULL || g_muted_nicks == NULL || g_num_muted_users == 0) {
        return 0; 
    }

    int found_idx = -1;
    for (int i = 0; i < g_num_muted_users; i++) { 
        if (g_muted_nicks[i] != NULL && strcmp(nick, g_muted_nicks[i]) == 0) {
            found_idx = i;
            break;
        }
    }

    if (found_idx == -1) {
        app_log(proc_tag, "MUTE_REMOVE", "User '%s' not found in mute list for unmuting.", nick);
        return 0; 
    }

    free(g_muted_nicks[found_idx]); 

    if (found_idx < g_num_muted_users - 1) {
        g_muted_nicks[found_idx] = g_muted_nicks[g_num_muted_users - 1];
    }
    
    g_num_muted_users--;
    g_muted_nicks[g_num_muted_users] = NULL; 
    
    if (g_num_muted_users == 0) {
        free(g_muted_nicks);
        g_muted_nicks = NULL;
    } else {
        char **shrunk_list = (char **)realloc(g_muted_nicks, (g_num_muted_users + 1) * sizeof(char*));
        if (shrunk_list != NULL) {
            g_muted_nicks = shrunk_list;
        } 
    }
    app_log(proc_tag, "MUTE_REMOVE", "User '%s' removed from mute list. Total muted: %d.", nick, g_num_muted_users);
    return save_muted_users_to_file(MUTED_USERS_FILE_PATH); 
}