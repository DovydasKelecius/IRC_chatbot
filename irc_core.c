#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "irc_bot.h"
#include <string.h> 

void SIG_parent_handler(int numSignal) {
    char parent_tag[32];
    snprintf(parent_tag, sizeof(parent_tag), "Parent %d", getpid());
    if (numSignal == SIGINT || numSignal == SIGTERM) {
        shutdown_requested = 1;
        app_log(parent_tag, "INFO", "Shutdown requested by signal %d.", numSignal);
    }
}

void SIG_child_handler(int numSignal) {
    if (numSignal == SIGTERM) {
        child_exit_flag = 1;
    }
}

int initSIGNALS(void) {
    char parent_tag[32];
    snprintf(parent_tag, sizeof(parent_tag), "Parent %d", getpid());
    if (signal(SIGINT, SIG_parent_handler) == SIG_ERR) {
        app_log(parent_tag, "ERROR", "signal(SIGINT) failed: %s", strerror(errno));
        return EXIT_FAILURE;
    }
    if (signal(SIGTERM, SIG_parent_handler) == SIG_ERR) {
        app_log(parent_tag, "ERROR", "signal(SIGTERM) failed: %s", strerror(errno));
        return EXIT_FAILURE;
    }
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) { 
        app_log(parent_tag, "ERROR", "signal(SIGPIPE) failed: %s", strerror(errno));
        return EXIT_FAILURE;
    }
    app_log(parent_tag, "INFO", "SIGINT, SIGTERM, SIGPIPE handlers initialized.");
    return EXIT_SUCCESS;
}

int initSemaphores(void) {
    char parent_tag[32];
    snprintf(parent_tag, sizeof(parent_tag), "Parent %d", getpid());
    socket_lock = (sem_t *)mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (socket_lock == MAP_FAILED) {
        app_log(parent_tag, "ERROR", "mmap for semaphore failed: %s", strerror(errno));
        return EXIT_FAILURE;
    }
    if (sem_init(socket_lock, 1, 1) == -1) {
        app_log(parent_tag, "ERROR", "sem_init failed: %s", strerror(errno));
        munmap(socket_lock, sizeof(sem_t));
        socket_lock = NULL;
        return EXIT_FAILURE;
    }
    app_log(parent_tag, "INFO", "Shared memory semaphore initialized.");
    return EXIT_SUCCESS;
}

void cleanup_semaphore(void) {
    char parent_tag[32];
    snprintf(parent_tag, sizeof(parent_tag), "Parent %d", getpid());
    if (socket_lock != NULL) {
        if (sem_destroy(socket_lock) == -1) 
            app_log(parent_tag, "ERROR", "sem_destroy failed: %s", strerror(errno));
        if (munmap(socket_lock, sizeof(sem_t)) == -1) 
            app_log(parent_tag, "ERROR", "munmap for semaphore failed: %s", strerror(errno));
        socket_lock = NULL;
        app_log(parent_tag, "INFO", "Shared memory semaphore cleaned up.");
    }
}


void mainLoop(char recv_buffer[], size_t recv_buffer_size, int *child_status) {
    char parent_tag[32];
    snprintf(parent_tag, sizeof(parent_tag), "Parent %d", getpid());

    fd_set read_fds;
    struct timeval tv;

    while (!shutdown_requested) {
        FD_ZERO(&read_fds);
        if (socket_fd != -1) FD_SET(socket_fd, &read_fds);
        else { 
            app_log(parent_tag, "ERROR", "Socket FD is -1 in mainLoop. Requesting shutdown.");
            shutdown_requested = 1; 
            continue; 
        }

        tv.tv_sec = 1; tv.tv_usec = 0;
        int activity = select(socket_fd + 1, &read_fds, NULL, NULL, &tv);

        if (activity < 0) {
            if (errno == EINTR) { if (shutdown_requested) break; continue; }
            app_log(parent_tag, "ERROR", "select error in main loop: %s", strerror(errno));
            shutdown_requested = 1; break;
        }

        if (activity > 0 && FD_ISSET(socket_fd, &read_fds)) {
            clearBuffer(recv_buffer, recv_buffer_size);
            ssize_t bytes_received = recv(socket_fd, recv_buffer, recv_buffer_size - 1, 0);
            if (bytes_received <= 0) {
                if (bytes_received == 0) app_log(parent_tag, "INFO", "Server disconnected.");
                else app_log(parent_tag, "ERROR", "recv error in main loop: %s", strerror(errno));
                shutdown_requested = 1; break;
            }
            recv_buffer[bytes_received] = '\0';

            char *line_saveptr_outer;
            char *full_line = strtok_r(recv_buffer, "\r\n", &line_saveptr_outer);
            while (full_line != NULL) {
                char *line_copy_for_parsing = strdup(full_line); 
                if (!line_copy_for_parsing) {
                    app_log(parent_tag, "ERROR", "strdup failed in mainLoop: %s", strerror(errno));
                    full_line = strtok_r(NULL, "\r\n", &line_saveptr_outer);
                    continue;
                }
                app_log(parent_tag, "RECV", "%s", line_copy_for_parsing);
                
                if (strncmp(line_copy_for_parsing, "PING :", 6) == 0) {
                    send_irc(socket_fd, "PONG :%s", line_copy_for_parsing + 6);
                } else if (line_copy_for_parsing[0] == ':') {
                    char *sender_nick_dup = NULL; 
                    char *command = NULL;
                    char *target = NULL;
                    char *message_text_ptr = NULL; 
                    char *saveptr_inner; 

                    char *source_full = strtok_r(line_copy_for_parsing + 1, " ", &saveptr_inner);
                    if (source_full) {
                        char *nick_part_only = strtok(source_full, "!"); 
                        if (nick_part_only) sender_nick_dup = strdup(nick_part_only); 
                    }
                    
                    if(sender_nick_dup) command = strtok_r(NULL, " ", &saveptr_inner);
                    if(command) target = strtok_r(NULL, " ", &saveptr_inner);
                    if(target) {
                        message_text_ptr = strtok_r(NULL, "", &saveptr_inner); 
                        if (message_text_ptr && message_text_ptr[0] == ':') {
                            message_text_ptr++; 
                        } else {
                            if (command && strcmp(command, "PRIVMSG") == 0 && (!message_text_ptr || message_text_ptr[0] != ':')) {
                                message_text_ptr = NULL; 
                            }
                        }
                    }
                    
                    if (sender_nick_dup && command && target && strcmp(command, "353") == 0) {
                        char *channel_name_353 = NULL;
                        char *users_list_start = NULL;
                        char *eq_sign_or_type = strtok_r(NULL, " ", &saveptr_inner); 
                        if(eq_sign_or_type) channel_name_353 = strtok_r(NULL, " ", &saveptr_inner);
                        if (channel_name_353) {
                             users_list_start = strtok_r(NULL, "", &saveptr_inner); 
                             if(users_list_start && users_list_start[0] == ':') users_list_start++;
                        }
                        if (ADMIN_CHANNEL_NAME_CONST && channel_name_353 && users_list_start) {
                             send_irc(socket_fd, "PRIVMSG %s :Users in %s: %s", ADMIN_CHANNEL_NAME_CONST, channel_name_353, users_list_start);
                        }
                    }

                    else if (sender_nick_dup && command && target && message_text_ptr && strcmp(command, "PRIVMSG") == 0) {
                        app_log(parent_tag, "MSG", "<%s> [%s] %s", target, sender_nick_dup, message_text_ptr);

                        if (is_user_globally_muted(sender_nick_dup)) {
                            app_log(parent_tag, "INFO", "Ignored PRIVMSG from globally muted user: %s in %s", sender_nick_dup, target);
                        } else if (is_other_bot_nick(sender_nick_dup)) {
                            app_log(parent_tag, "INFO", "Ignored PRIVMSG from other bot: %s", sender_nick_dup);
                        } else if (strcmp(target, NICK) == 0) { 
                            app_log(parent_tag, "CMD", "Received DM from %s: %s", sender_nick_dup, message_text_ptr);
                            send_irc(socket_fd, "PRIVMSG %s :I'm a channel bot, please talk to me in my channels!", sender_nick_dup);
                        } else if (ADMIN_CHANNEL_NAME_CONST && strcmp(target, ADMIN_CHANNEL_NAME_CONST) == 0) {
                            app_log(parent_tag, "CMD", "Admin Channel <%s> from [%s]: %s", target, sender_nick_dup, message_text_ptr);
                            if (strncmp(message_text_ptr, "!mute ", 6) == 0) {
                                char *nick_to_mute = message_text_ptr + 6;
                                app_log(parent_tag, "CMD_ACT", "Attempting to mute '%s' by %s.", nick_to_mute, sender_nick_dup);
                                if (strlen(nick_to_mute) > 0 && strlen(nick_to_mute) < MAX_NICK_LEN) {
                                    if (add_muted_user(nick_to_mute) == 0) { 
                                        send_irc(socket_fd, "PRIVMSG %s :User %s has been globally muted.", ADMIN_CHANNEL_NAME_CONST, nick_to_mute);
                                    } else {
                                        send_irc(socket_fd, "PRIVMSG %s :Failed to mute user %s.", ADMIN_CHANNEL_NAME_CONST, nick_to_mute);
                                    }
                                } else {
                                    send_irc(socket_fd, "PRIVMSG %s :Invalid nick for !mute command.", ADMIN_CHANNEL_NAME_CONST);
                                }
                            } else if (strncmp(message_text_ptr, "!unmute ", 8) == 0) {
                                char *nick_to_unmute = message_text_ptr + 8;
                                app_log(parent_tag, "CMD_ACT", "Attempting to unmute '%s' by %s.", nick_to_unmute, sender_nick_dup);
                                if (strlen(nick_to_unmute) > 0 && strlen(nick_to_unmute) < MAX_NICK_LEN) {
                                    if (remove_muted_user(nick_to_unmute) == 0) { 
                                        send_irc(socket_fd, "PRIVMSG %s :User %s has been unmuted.", ADMIN_CHANNEL_NAME_CONST, nick_to_unmute);
                                    } else {
                                        send_irc(socket_fd, "PRIVMSG %s :User %s was not found in mute list or failed to unmute.", ADMIN_CHANNEL_NAME_CONST, nick_to_unmute);
                                    }
                                } else {
                                    send_irc(socket_fd, "PRIVMSG %s :Invalid nick for !unmute command.", ADMIN_CHANNEL_NAME_CONST);
                                }
                            } 
                            else if (strncmp(message_text_ptr, "!ask ", 5) == 0) { // !ask command
                                char* user_prompt = message_text_ptr + 5;
                                if (strlen(user_prompt) > 0) {
                                    app_log(parent_tag, "CMD_AI", "AI Ask from [%s] in <%s>: %s", sender_nick_dup, target, user_prompt);
                                    if (g_channel_infos && g_channel_infos[0].persona) {
                                         app_log(parent_tag, "CMD_AI", "!ask command ignored in admin channel. Use in worker channels.");
                                         send_irc(socket_fd, "PRIVMSG %s :The !ask command is for use in my other managed channels.", ADMIN_CHANNEL_NAME_CONST);

                                    } else {
                                         app_log(parent_tag, "WARN", "Admin channel persona not found for !ask command.");
                                    }
                                } else {
                                    send_irc(socket_fd, "PRIVMSG %s :Usage: !ask <your question>", ADMIN_CHANNEL_NAME_CONST);
                                }
                            }
                            else if (strcmp(message_text_ptr, "!status") == 0) { // !status command
                                app_log(parent_tag, "CMD", "User '%s' requested !status in admin channel %s.", sender_nick_dup, target);
                                send_irc(socket_fd, "PRIVMSG %s :--- Bot Status ---", ADMIN_CHANNEL_NAME_CONST);
                                for (int i = 0; i < numWorkerChildren; i++) {
                                    if (g_channel_infos && g_channel_infos[i+1].name != NULL) { // Workers handle channels from index 1
                                        if (worker_child_pids && worker_child_pids[i] > 0 && kill(worker_child_pids[i], 0) == 0) {
                                            send_irc(socket_fd, "PRIVMSG %s :Worker for %s (PID %d) is ACTIVE.", ADMIN_CHANNEL_NAME_CONST, g_channel_infos[i+1].name, worker_child_pids[i]);
                                        } else {
                                            send_irc(socket_fd, "PRIVMSG %s :Worker for %s is INACTIVE/TERMINATED.", ADMIN_CHANNEL_NAME_CONST, g_channel_infos[i+1].name);
                                        }
                                    }
                                    usleep(100000);
                                }
                                if (pinger_child_pid > 0 && kill(pinger_child_pid, 0) == 0) {
                                    send_irc(socket_fd, "PRIVMSG %s :Pinger (PID %d) is ACTIVE.", ADMIN_CHANNEL_NAME_CONST, pinger_child_pid);
                                } else {
                                    send_irc(socket_fd, "PRIVMSG %s :Pinger is INACTIVE/TERMINATED.", ADMIN_CHANNEL_NAME_CONST);
                                }
                                send_irc(socket_fd, "PRIVMSG %s :--- End Status ---", ADMIN_CHANNEL_NAME_CONST);
                            } else if (strcmp(message_text_ptr, "!users") == 0) {

                                app_log(parent_tag, "CMD", "User '%s' requested !users in admin channel.", sender_nick_dup);
                                send_irc(socket_fd, "PRIVMSG %s :Requesting user lists for all managed channels...", ADMIN_CHANNEL_NAME_CONST);
                                // Send NAMES for each managed channel and forward the user list to the admin channel
                                for(int i=0; i < numWorkerChildren && g_channel_infos && g_channel_infos[i+1].name != NULL; ++i) {
                                    send_irc(socket_fd, "NAMES %s", g_channel_infos[i+1].name);
                                    usleep(200000);
                                }
                            }
                        } else { // Regular managed worker channel
                            int worker_idx = -1;
                            const char* channel_persona = "You are a helpful assistant."; // Default
                            
                            for(int i = 0; i < numWorkerChildren; ++i) {
                                // worker_child_pids[i] corresponds to g_channel_infos[i+1]
                                if(g_channel_infos && g_channel_infos[i+1].name && strcmp(target, g_channel_infos[i+1].name) == 0) {
                                    worker_idx = i;
                                    if (g_channel_infos[i+1].persona) {
                                        channel_persona = g_channel_infos[i+1].persona;
                                    }
                                    break;
                                }
                            }

                            if (worker_idx != -1 && worker_write_pipe_fds && worker_write_pipe_fds[worker_idx] != -1) {
                                if (strncmp(message_text_ptr, "!ask ", 5) == 0) {
                                    char* user_prompt = message_text_ptr + 5;
                                    if (strlen(user_prompt) > 0) {
                                        app_log(parent_tag, "CMD_AI", "AI Ask from [%s] in <%s>: %s. Forwarding to worker %d.", sender_nick_dup, target, user_prompt, worker_idx);
                                        char pipe_msg[MAX_PIPE_MSG_LEN];
                                        // Pipe Format: "ASK\tSENDER_NICK\tPERSONA\tPROMPT\n"
                                        snprintf(pipe_msg, sizeof(pipe_msg), "ASK%c%s%c%s%c%s\n",
                                                 PIPE_MSG_DELIMITER_CHAR, sender_nick_dup,
                                                 PIPE_MSG_DELIMITER_CHAR, channel_persona,
                                                 PIPE_MSG_DELIMITER_CHAR, user_prompt);
                                        
                                        if (write(worker_write_pipe_fds[worker_idx], pipe_msg, strlen(pipe_msg)) == -1 && errno != EAGAIN) {
                                            app_log(parent_tag, "ERROR", "Write to worker pipe for %s failed: %s", target, strerror(errno));
                                        }
                                    } else {
                                        send_irc(socket_fd, "PRIVMSG %s :Usage: !ask <your question>", target);
                                    }
                                } else if (strcmp(message_text_ptr, "!hello") == 0) {
                                    char pipe_msg[MAX_PIPE_MSG_LEN];
                                    snprintf(pipe_msg, sizeof(pipe_msg), "HELLO%c%s%c%s\n", PIPE_MSG_DELIMITER_CHAR, sender_nick_dup, PIPE_MSG_DELIMITER_CHAR, message_text_ptr);
                                    app_log(parent_tag, "PIPE_SEND", "To worker for %s: HELLO command", target);
                                    if (write(worker_write_pipe_fds[worker_idx], pipe_msg, strlen(pipe_msg)) == -1 && errno != EAGAIN) {
                                        app_log(parent_tag, "ERROR", "Write to worker pipe for %s failed: %s", target, strerror(errno));
                                    }
                                } else if (strcmp(message_text_ptr, "!status") == 0) { 
                                     send_irc(socket_fd, "PRIVMSG %s :Hi %s! I'm worker for this channel. For detailed bot status, ask in %s", target, sender_nick_dup, ADMIN_CHANNEL_NAME_CONST ? ADMIN_CHANNEL_NAME_CONST : "the admin channel");
                                }
                            }
                        }
                    }
                    if (sender_nick_dup) free(sender_nick_dup); 
                }
                free(line_copy_for_parsing);
                full_line = strtok_r(NULL, "\r\n", &line_saveptr_outer);
            }
        }

        pid_t terminated_pid;
        while ((terminated_pid = waitpid(-1, child_status, WNOHANG)) > 0) {
            char exit_reason[100];
            if (WIFEXITED(*child_status)) snprintf(exit_reason, sizeof(exit_reason), "exited normally (status %d)", WEXITSTATUS(*child_status));
            else if (WIFSIGNALED(*child_status)) snprintf(exit_reason, sizeof(exit_reason), "terminated by signal %d", WTERMSIG(*child_status));
            else snprintf(exit_reason, sizeof(exit_reason), "terminated abnormally");
            
            app_log(parent_tag, "INFO", "Child PID %d %s.", terminated_pid, exit_reason);

            if (terminated_pid == pinger_child_pid) { 
                app_log(parent_tag, "CRITICAL", "PINGER (PID %d) terminated! Requesting shutdown.", pinger_child_pid);
                pinger_child_pid = -1; shutdown_requested = 1; 
            } else {
                if (worker_child_pids != NULL) {
                    for (int i = 0; i < numWorkerChildren; i++) { // Iterate numWorkerChildren
                        if (worker_child_pids[i] == terminated_pid) {
                            app_log(parent_tag, "INFO", "Worker for %s (PID %d) terminated.", (g_channel_infos && g_channel_infos[i+1].name) ? g_channel_infos[i+1].name : "N/A", terminated_pid);
                            worker_child_pids[i] = -1;
                            if (worker_write_pipe_fds && worker_write_pipe_fds[i] != -1) { close(worker_write_pipe_fds[i]); worker_write_pipe_fds[i] = -1; }
                            break;
                        }
                    }
                }
            }
        }
    }
}

void softShutdown(int *child_status) {
    char parent_tag[32];
    snprintf(parent_tag, sizeof(parent_tag), "Parent %d", getpid());
    app_log(parent_tag, "INFO", "Initializing graceful shutdown...");
    
    if (socket_fd != -1) {
        send_irc(socket_fd, "QUIT :%s", QUIT_MESSAGE); 
    }
    
    usleep(500000); 

    app_log(parent_tag, "INFO", "Sending SIGTERM to child processes...");
    if (pinger_child_pid > 0) {
        app_log(parent_tag, "INFO", "Sending SIGTERM to Pinger PID %d.", pinger_child_pid);
        if(kill(pinger_child_pid, SIGTERM) == -1 && errno != ESRCH) 
            app_log(parent_tag, "ERROR", "kill pinger failed: %s", strerror(errno));
    }
    
    if (worker_child_pids != NULL) {
        for (int i = 0; i < numWorkerChildren; i++) { // Iterate numWorkerChildren
            if (worker_write_pipe_fds && worker_write_pipe_fds[i] != -1) {
                app_log(parent_tag, "INFO", "Closing pipe to worker for %s (PID %d).", (g_channel_infos && g_channel_infos[i+1].name) ? g_channel_infos[i+1].name : "N/A", worker_child_pids[i]);
                close(worker_write_pipe_fds[i]);
                worker_write_pipe_fds[i] = -1;
            }
            if (worker_child_pids[i] > 0) {
                app_log(parent_tag, "INFO", "Sending SIGTERM to Worker for %s (PID %d).", (g_channel_infos && g_channel_infos[i+1].name) ? g_channel_infos[i+1].name : "N/A", worker_child_pids[i]);
                if(kill(worker_child_pids[i], SIGTERM) == -1 && errno != ESRCH)
                     app_log(parent_tag, "ERROR", "kill worker for %s failed: %s", (g_channel_infos && g_channel_infos[i+1].name) ? g_channel_infos[i+1].name : "N/A", strerror(errno));
            }
        }
    }

    app_log(parent_tag, "INFO", "Waiting for children to terminate...");
    time_t shutdown_start_time = time(NULL); int children_still_active = 1;
    while(children_still_active){
        children_still_active = 0; 
        if (pinger_child_pid > 0 && kill(pinger_child_pid, 0) == 0) children_still_active++;
        if (worker_child_pids != NULL) for(int i=0; i<numWorkerChildren; ++i) if(worker_child_pids[i] > 0 && kill(worker_child_pids[i], 0) == 0) children_still_active++;
        if(!children_still_active) {
            app_log(parent_tag, "INFO", "All children appear to have exited or were never active.");
            break;
        }

        if (time(NULL) - shutdown_start_time > 10) { 
            app_log(parent_tag, "WARN", "Timeout waiting for children. Sending SIGKILL.");
            if (pinger_child_pid > 0 && kill(pinger_child_pid,0)==0) {
                app_log(parent_tag, "WARN", "SIGKILL to Pinger PID %d.", pinger_child_pid);
                kill(pinger_child_pid, SIGKILL);
            }
            if (worker_child_pids != NULL) {
                for (int i=0; i<numWorkerChildren; i++) {
                    if (worker_child_pids[i]>0 && kill(worker_child_pids[i],0)==0) {
                        app_log(parent_tag, "WARN", "SIGKILL to Worker for %s (PID %d).", (g_channel_infos && g_channel_infos[i+1].name) ? g_channel_infos[i+1].name : "N/A", worker_child_pids[i]);
                        kill(worker_child_pids[i], SIGKILL);
                    }
                }
            }
            break; 
        }
        pid_t terminated_pid = waitpid(-1, child_status, WNOHANG); 
        if (terminated_pid > 0) {
             app_log(parent_tag, "INFO", "Child PID %d reaped during shutdown.", terminated_pid);
             if(terminated_pid == pinger_child_pid) pinger_child_pid = -1;
             else if(worker_child_pids != NULL) for(int i=0; i<numWorkerChildren; ++i) if(worker_child_pids[i] == terminated_pid) worker_child_pids[i] = -1;
        } else if (terminated_pid == -1 && errno == ECHILD) { 
            app_log(parent_tag, "INFO", "waitpid reports no more children (ECHILD).");
            break; 
        }
        if(children_still_active) usleep(100000); 
    }
    app_log(parent_tag, "INFO", "All child processes have been handled.");

    if (socket_fd != -1) { close(socket_fd); socket_fd = -1; app_log(parent_tag, "INFO", "IRC socket closed."); }
    
    if (worker_write_pipe_fds != NULL) { 
        for(int i=0; i < numWorkerChildren; ++i) if(worker_write_pipe_fds[i] != -1) close(worker_write_pipe_fds[i]); 
        free(worker_write_pipe_fds); worker_write_pipe_fds = NULL; 
    }
    if (worker_child_pids != NULL) { free(worker_child_pids); worker_child_pids = NULL; }
    
    app_log(parent_tag, "INFO", "Graceful shutdown complete.");
}