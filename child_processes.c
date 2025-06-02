#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "irc_bot.h"
#include "gemini_integration.h"

// Pinger Child Process
void child_PING(int sock_param, const char* ip_override) {
    char pinger_tag[32];
    snprintf(pinger_tag, sizeof(pinger_tag), "Pinger %d", getpid());

    app_log(pinger_tag, "DEBUG", "Pinger process entered child_PING function.");

    if (signal(SIGTERM, SIG_child_handler) == SIG_ERR) {
        app_log(pinger_tag, "FATAL", "signal(SIGTERM) failed: %s", strerror(errno));
        _exit(EXIT_FAILURE); // Exit immediately if signal handler setup fails
    }
    signal(SIGINT, SIG_IGN); 

    const char* ping_ip = (ip_override && ip_override[0]) ? ip_override : SERVER_IP;

    app_log(pinger_tag, "INFO", "Pinger Started. Will ping server %s every %d seconds via FD %d.", ping_ip, PING_INTERVAL_SECONDS, sock_param);

    while (!child_exit_flag) {
        for (int i = 0; i < PING_INTERVAL_SECONDS && !child_exit_flag; ++i) {
            sleep(1);
        }
        if (child_exit_flag) break; // Break if flag was set during sleep loop

        send_irc(sock_param, "PING :%s", ping_ip);
    }
    app_log(pinger_tag, "INFO", "Pinger Exiting due to child_exit_flag.");
    _exit(EXIT_SUCCESS);
}

// Worker Child Process - THE SLAVES
void child_WORKER(int worker_id, const ChannelInfo* channel_info, int local_socket_fd, int pipe_read_fd) {
    char worker_tag[128];
    snprintf(worker_tag, sizeof(worker_tag), "Worker %d (%s) %d", worker_id + 1, channel_info->name, getpid());

    app_log(worker_tag, "DEBUG", "Worker process entered child_WORKER function.");

    if (signal(SIGTERM, SIG_child_handler) == SIG_ERR) {
        app_log(worker_tag, "FATAL", "signal(SIGTERM) failed: %s", strerror(errno));
        _exit(EXIT_FAILURE);
    }
    signal(SIGINT, SIG_IGN);

    if (fcntl(pipe_read_fd, F_SETFL, O_NONBLOCK) == -1) {
        app_log(worker_tag, "FATAL", "fcntl F_SETFL O_NONBLOCK on pipe failed: %s", strerror(errno));
        _exit(EXIT_FAILURE);
    }

    app_log(worker_tag, "INFO", "Worker Started. Persona: '%s'. Pipe_read_fd: %d, socket_fd: %d.",
            channel_info->persona ? channel_info->persona : "Default", pipe_read_fd, local_socket_fd);

    fd_set current_read_fds;
    struct timeval tv;
    char pipe_buffer[MAX_PIPE_MSG_LEN];

    // Read Gemini API Key once at the start of the child worker
    const char *api_key_for_child = getenv("GEMINIAI_API_KEY");
    // If API key is missing, just log and continue (AI features will be disabled)
    if (api_key_for_child == NULL || strlen(api_key_for_child) == 0) {
        app_log(worker_tag, "WARN", "GEMINIAI_API_KEY not found by worker. AI features will be disabled for this worker.");
        api_key_for_child = NULL; 
    }

    while (!child_exit_flag) {

        sleep(30); // Sleep to reduce CPU usage
        send_irc(local_socket_fd, "PRIVMSG %s :My commands [ID %d]: !ask <prompt> !hello.", channel_info->name, worker_id + 1);


        FD_ZERO(&current_read_fds);
        FD_SET(pipe_read_fd, &current_read_fds);

        tv.tv_sec = 1; tv.tv_usec = 0; // Short timeout to check child_exit_flag regularly
        int activity = select(pipe_read_fd + 1, &current_read_fds, NULL, NULL, &tv);

        if (activity < 0) {
            if (errno == EINTR && !child_exit_flag) continue; // Interrupted by a signal, but not shutdown
            app_log(worker_tag, "ERROR", "select error: %s", strerror(errno));
            break;
        }

        if (FD_ISSET(pipe_read_fd, &current_read_fds)) {
            app_log(worker_tag, "DEBUG", "Pipe has data. Attempting to read...");
            clearBuffer(pipe_buffer, sizeof(pipe_buffer));
            ssize_t bytes_read = read(pipe_read_fd, pipe_buffer, sizeof(pipe_buffer) - 1);

            if (bytes_read > 0) {
                pipe_buffer[bytes_read] = '\0';
                // Remove trailing newline if present
                if (bytes_read > 0 && pipe_buffer[bytes_read - 1] == '\n') pipe_buffer[bytes_read - 1] = '\0';

                app_log(worker_tag, "PIPE_RECV", "Raw: '%s' (Bytes: %zd)", pipe_buffer, bytes_read);

                // Use a copy for strtok as strtok modifies the string in place
                char *pipe_buffer_copy = strdup(pipe_buffer);
                if (!pipe_buffer_copy) {
                    app_log(worker_tag, "ERROR", "strdup failed for pipe message: %s", strerror(errno));
                    continue;
                }

                char *command_type = strtok(pipe_buffer_copy, PIPE_MSG_DELIMITER_STR);
                app_log(worker_tag, "DEBUG", "Parsed command type: '%s'", command_type ? command_type : "NULL");


                if (command_type != NULL) {
                    if (strcmp(command_type, "HELLO") == 0) {
                        char *sender_nick = strtok(NULL, PIPE_MSG_DELIMITER_STR);
                        char *message_text = strtok(NULL, "");
                        if (sender_nick && message_text) {
                            app_log(worker_tag, "DEBUG", "HELLO command: sender='%s', message='%s'", sender_nick, message_text);
                            send_irc(local_socket_fd, "PRIVMSG %s :Hello %s! Worker for %s received your message: \"%s\"",
                                     channel_info->name, sender_nick, channel_info->name, message_text);
                        } else {
                             app_log(worker_tag, "WARN", "HELLO command missing parts. Raw: '%s'", pipe_buffer);
                        }
                    } else if (strcmp(command_type, "ASK") == 0) {
                        char *sender_nick = strtok(NULL, PIPE_MSG_DELIMITER_STR);
                        char *persona_from_pipe = strtok(NULL, PIPE_MSG_DELIMITER_STR); // This is the channel's specific persona
                        char *user_prompt = strtok(NULL, "");

                        app_log(worker_tag, "DEBUG", "ASK command: sender='%s', persona='%s', prompt='%s'",
                                sender_nick ? sender_nick : "NULL",
                                persona_from_pipe ? persona_from_pipe : "NULL",
                                user_prompt ? user_prompt : "NULL");


                        if (sender_nick && persona_from_pipe && user_prompt) {
                            if (api_key_for_child) { // Check if API key was successfully loaded
                                app_log(worker_tag, "AI_REQUEST", "Processing !ask from [%s] with persona: '%s'. Prompt: '%s'",
                                        sender_nick, persona_from_pipe, user_prompt);

                                // CALL GEMINI API
                                char* ai_response = get_gemini_response(persona_from_pipe, user_prompt, api_key_for_child);

                                if (ai_response) {
                                    for (char *p = ai_response; *p; ++p) {
                                        if (*p == '\n' || *p == '\r') {
                                            *p = ' ';
                                        }
                                    }
                                    // Acquire semaphore lock before sending IRC message
                                    app_log(worker_tag, "DEBUG", "Attempting to acquire socket_lock for AI response.");
                                    send_irc(local_socket_fd, "PRIVMSG %s :%s: %s", channel_info->name, sender_nick, ai_response);
                                    app_log(worker_tag, "DEBUG", "Socket_lock released for AI response.");
                                    free(ai_response); // Free the dynamically allocated response
                                } else {
                                    app_log(worker_tag, "AI_ERROR", "Failed to get AI response for prompt from %s.", sender_nick);
                                    send_irc(local_socket_fd, "PRIVMSG %s :%s, I encountered an error trying to process your request.", channel_info->name, sender_nick);
                                }
                            } else {
                                app_log(worker_tag, "AI_SKIP", "Gemini API key not available. Cannot process !ask from %s.", sender_nick);
                                send_irc(local_socket_fd, "PRIVMSG %s :%s, AI features are currently disabled.", channel_info->name, sender_nick);
                            }
                        } else {
                             app_log(worker_tag, "WARN", "Could not parse ASK command from pipe: '%s'", pipe_buffer);
                        }
                    } else {
                        app_log(worker_tag, "WARN", "Unknown command type from pipe: '%s'", command_type);
                    }
                }
                free(pipe_buffer_copy); // Free the duplicated string
            } else if (bytes_read == 0) {
                app_log(worker_tag, "INFO", "Parent closed pipe. Assuming shutdown.");
                child_exit_flag = 1; // Signal for child to exit
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                app_log(worker_tag, "ERROR", "Read from pipe failed: %s", strerror(errno));
                child_exit_flag = 1; // Signal for child to exit on critical error
            } else {
                app_log(worker_tag, "DEBUG", "Pipe is empty (EAGAIN/EWOULDBLOCK). No message to process.");
            }
        }
    }
    close(pipe_read_fd); // Close the read end of the pipe
    app_log(worker_tag, "INFO", "Worker Exiting due to child_exit_flag.");
    _exit(EXIT_SUCCESS); // Ensure child process exits cleanly
}

// Forking Child Processes
int forkChildren(const char *ip_override) {
    char parent_tag[32];
    snprintf(parent_tag, sizeof(parent_tag), "Parent %d", getpid());

    app_log(parent_tag, "INFO", "Forking pinger child...");
    pinger_child_pid = fork();
    if (pinger_child_pid < 0) {
        app_log(parent_tag, "ERROR", "Fork for PINGER failed: %s", strerror(errno));
        return EXIT_FAILURE;
    } else if (pinger_child_pid == 0) {
        child_PING(socket_fd, ip_override); // Child will _exit here
    }
    app_log(parent_tag, "INFO", "Forked PINGER child with PID %d.", pinger_child_pid);

    app_log(parent_tag, "INFO", "Preparing to fork %d worker children.", numWorkerChildren);

    if (numWorkerChildren > 0) {
        worker_child_pids = (pid_t *)malloc(numWorkerChildren * sizeof(pid_t));
        worker_write_pipe_fds = (int *)malloc(numWorkerChildren * sizeof(int));
        if (!worker_child_pids || !worker_write_pipe_fds) {
            app_log(parent_tag, "ERROR", "Malloc for worker PIDs or pipes failed: %s", strerror(errno));
            if (pinger_child_pid > 0) kill(pinger_child_pid, SIGTERM); // Terminate pinger if workers can't be set up
            free(worker_child_pids); worker_child_pids = NULL;
            free(worker_write_pipe_fds); worker_write_pipe_fds = NULL;
            return EXIT_FAILURE;
        }
        for (int i = 0; i < numWorkerChildren; ++i) {
            worker_child_pids[i] = -1;
            worker_write_pipe_fds[i] = -1;
        }

        for (int i = 0; i < numWorkerChildren; i++) {
            // Worker 'i' will handle g_channel_infos[i+1] (channel after admin channel, which is g_channel_infos[0])
            if (g_channel_infos == NULL || (i + 1) >= numChildren || g_channel_infos[i+1].name == NULL) {
                 app_log(parent_tag, "ERROR", "Cannot fork worker %d, channel info is missing or invalid for g_channel_infos[%d].", i, i + 1);
                 if (pinger_child_pid > 0) kill(pinger_child_pid, SIGTERM);
                 for (int j = 0; j < i; j++) { // Clean up previously forked workers and pipes
                     if (worker_child_pids[j] > 0) kill(worker_child_pids[j], SIGTERM);
                     if (worker_write_pipe_fds[j] != -1) close(worker_write_pipe_fds[j]);
                 }
                 free(worker_child_pids); worker_child_pids = NULL;
                 free(worker_write_pipe_fds); worker_write_pipe_fds = NULL;
                 return EXIT_FAILURE;
            }

            int current_pipe_fds[2];
            if (pipe(current_pipe_fds) == -1) {
                app_log(parent_tag, "ERROR", "pipe() creation failed for worker %d (%s): %s", i, g_channel_infos[i+1].name, strerror(errno));
                // Cleanup already forked children and pipes
                if (pinger_child_pid > 0) kill(pinger_child_pid, SIGTERM);
                for (int j = 0; j < i; j++) {
                    if (worker_child_pids[j] > 0) kill(worker_child_pids[j], SIGTERM);
                    if (worker_write_pipe_fds[j] != -1) close(worker_write_pipe_fds[j]);
                }
                free(worker_child_pids); worker_child_pids = NULL;
                free(worker_write_pipe_fds); worker_write_pipe_fds = NULL;
                return EXIT_FAILURE;
            }

            app_log(parent_tag, "INFO", "Forking WORKER child %d for channel %s...", i, g_channel_infos[i+1].name);
            pid_t worker_pid = fork();
            if (worker_pid < 0) {
                app_log(parent_tag, "ERROR", "Fork for WORKER %d (%s) failed: %s", i, g_channel_infos[i+1].name, strerror(errno));
                close(current_pipe_fds[0]); close(current_pipe_fds[1]); // Close pipe ends for failed fork
                // Cleanup already forked children and pipes
                if (pinger_child_pid > 0) kill(pinger_child_pid, SIGTERM);
                for (int j = 0; j < i; j++) {
                    if (worker_child_pids[j] > 0) kill(worker_child_pids[j], SIGTERM);
                    if (worker_write_pipe_fds[j] != -1) close(worker_write_pipe_fds[j]);
                }
                free(worker_child_pids); worker_child_pids = NULL;
                free(worker_write_pipe_fds); worker_write_pipe_fds = NULL;
                return EXIT_FAILURE;
            } else if (worker_pid == 0) { // Child (Worker)
                close(current_pipe_fds[1]); // Close write end in child
                // Free memory allocated in parent, as child has its own copy (before _exit)
                if (worker_write_pipe_fds != NULL) { free(worker_write_pipe_fds); worker_write_pipe_fds = NULL; }
                if (worker_child_pids != NULL) { free(worker_child_pids); worker_child_pids = NULL; }

                // Pass a pointer to the specific ChannelInfo struct for this worker
                child_WORKER(i, &g_channel_infos[i+1], socket_fd, current_pipe_fds[0]);
            } else { // Parent
                close(current_pipe_fds[0]); // Close read end in parent
                worker_write_pipe_fds[i] = current_pipe_fds[1];
                worker_child_pids[i] = worker_pid;
                app_log(parent_tag, "INFO", "Forked WORKER child %d (PID %d) for channel %s, pipe_write_fd: %d.",
                       i, worker_pid, g_channel_infos[i+1].name, worker_write_pipe_fds[i]);
            }
        }
    }
    app_log(parent_tag, "INFO", "Finished forking all child processes.");
    return EXIT_SUCCESS;
}