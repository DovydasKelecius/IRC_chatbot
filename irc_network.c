#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "irc_bot.h"
#include <string.h> 

void send_irc(int sock_param, const char *fmt, ...) {
    char message_content[900];
    char final_buffer[1024];
    va_list args;
    char proc_tag[64]; 

    pid_t current_pid = getpid();
    if (current_pid == getpgrp()) { 
         snprintf(proc_tag, sizeof(proc_tag), "Parent %d", current_pid);
    } else {
        snprintf(proc_tag, sizeof(proc_tag), "Child %d", current_pid);
    }


    if (sock_param < 0) {
        app_log(proc_tag, "ERROR", "Attempted send_irc on invalid socket parameter: %d.", sock_param);
        return;
    }

    va_start(args, fmt);
    vsnprintf(message_content, sizeof(message_content), fmt, args);
    va_end(args);

    snprintf(final_buffer, sizeof(final_buffer), "%.*s\r\n", (int)(sizeof(final_buffer) - 3), message_content);

    if (socket_lock != NULL) {
        while (sem_wait(socket_lock) == -1) {
            if (errno == EINTR) {
                if (shutdown_requested || child_exit_flag) {
                    app_log(proc_tag, "WARN", "Send aborted: shutdown during sem_wait.");
                    return;
                }
                continue;
            } else {
                app_log(proc_tag, "ERROR", "sem_wait failed in send_irc: %s", strerror(errno));
                return;
            }
        }
    } else {
        app_log(proc_tag, "WARN", "socket_lock is NULL, sending without lock.");
    }

    ssize_t bytes_to_send = strlen(final_buffer);
    ssize_t total_bytes_sent = 0;
    while (total_bytes_sent < bytes_to_send) {
        ssize_t bytes_sent_this_call = send(sock_param, final_buffer + total_bytes_sent, bytes_to_send - total_bytes_sent, 0);
        if (bytes_sent_this_call == -1) {
            if (errno == EINTR) {
                 if (shutdown_requested || child_exit_flag) {
                    app_log(proc_tag, "WARN", "Send aborted: shutdown during send.");
                    if (socket_lock != NULL) sem_post(socket_lock);
                    return;
                }
                continue;
            }
            app_log(proc_tag, "ERROR", "send() failed in send_irc: %s", strerror(errno));
            if (socket_lock != NULL) sem_post(socket_lock);
            return;
        }
        total_bytes_sent += bytes_sent_this_call;
    }
    
    char log_buffer[1024];
    strncpy(log_buffer, final_buffer, sizeof(log_buffer) -1);
    log_buffer[sizeof(log_buffer)-1] = '\0';
    size_t len = strlen(log_buffer);
    if (len >= 2 && log_buffer[len-2] == '\r' && log_buffer[len-1] == '\n') { // Check for \r\n
        log_buffer[len-2] = '\0';
    } else if (len >=1 && log_buffer[len-1] == '\n') { // Check for just \n
        log_buffer[len-1] = '\0';
    }
    app_log(proc_tag, "SENT", "%s", log_buffer);


    if (socket_lock != NULL) {
        if (sem_post(socket_lock) == -1) {
            app_log(proc_tag, "ERROR", "sem_post failed in send_irc: %s", strerror(errno));
        }
    }
}


int initSocket(const char *opt_ip, int opt_port) {
    char parent_tag[32];
    snprintf(parent_tag, sizeof(parent_tag), "Parent %d", getpid());
    struct sockaddr_in serv_addr;
    struct hostent *host_info;

    const char *server_ip = (opt_ip && strlen(opt_ip) > 0) ? opt_ip : SERVER_IP;
    int server_port = (opt_port > 0) ? opt_port : SERVER_PORT;

    app_log(parent_tag, "INFO", "Resolving hostname '%s'...", server_ip);
    host_info = gethostbyname(server_ip);
    if (host_info == NULL) {
        app_log(parent_tag, "ERROR", "Host resolution failed for %s: %s", server_ip, hstrerror(h_errno));
        return EXIT_FAILURE;
    }
    // Log the first IP address found
    if (host_info->h_addr_list[0] != NULL) {
        char ip_str_buffer[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, host_info->h_addr_list[0], ip_str_buffer, sizeof(ip_str_buffer));
        app_log(parent_tag, "INFO", "Hostname resolved to: %s", ip_str_buffer);
    } else {
        app_log(parent_tag, "WARN", "Hostname resolved but no IP address found in list.");
    }

    app_log(parent_tag, "INFO", "Creating socket...");
    socket_fd = socket(AF_INET, SOCK_STREAM, 0); 
    if (socket_fd < 0) {
        app_log(parent_tag, "ERROR", "Socket creation failed: %s", strerror(errno));
        return EXIT_FAILURE;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);
    memcpy(&serv_addr.sin_addr, host_info->h_addr_list[0], host_info->h_length);

    app_log(parent_tag, "INFO", "Connecting to IRC server %s:%d...", server_ip, server_port);

    // Set socket to non-blocking
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags == -1) flags = 0;
    fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);

    int connect_ret = connect(socket_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (connect_ret < 0) {
        if (errno == EINPROGRESS) {
            fd_set wfds;
            struct timeval tv;
            FD_ZERO(&wfds);
            FD_SET(socket_fd, &wfds);
            tv.tv_sec = 10; // 10 second timeout for connect
            tv.tv_usec = 0;
            int sel_ret;
            do {
                sel_ret = select(socket_fd + 1, NULL, &wfds, NULL, &tv);
            } while (sel_ret == -1 && errno == EINTR && !shutdown_requested);

            if (sel_ret > 0 && FD_ISSET(socket_fd, &wfds)) {
                int so_error = 0;
                socklen_t len = sizeof(so_error);
                getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
                if (so_error != 0) {
                    app_log(parent_tag, "ERROR", "Connection failed: %s", strerror(so_error));
                    close(socket_fd);
                    socket_fd = -1;
                    return EXIT_FAILURE;
                }
            } else if (sel_ret == 0) {
                app_log(parent_tag, "ERROR", "Connection timed out.");
                close(socket_fd);
                socket_fd = -1;
                return EXIT_FAILURE;
            } else {
                app_log(parent_tag, "ERROR", "Connection interrupted or failed: %s", strerror(errno));
                close(socket_fd);
                socket_fd = -1;
                return EXIT_FAILURE;
            }
        } else {
            app_log(parent_tag, "ERROR", "Connection failed: %s", strerror(errno));
            close(socket_fd);
            socket_fd = -1;
            return EXIT_FAILURE;
        }
    }

    // Restore socket to blocking mode
    fcntl(socket_fd, F_SETFL, flags);

    app_log(parent_tag, "INFO", "Connected to IRC server on socket FD %d.", socket_fd);
    return EXIT_SUCCESS;
}

int ircRegister(char recv_buffer[], size_t buffer_size) {
    char parent_tag[32];
    snprintf(parent_tag, sizeof(parent_tag), "Parent %d", getpid());

    app_log(parent_tag, "INFO", "Sending NICK %s", NICK);
    send_irc(socket_fd, "NICK %s", NICK); 
    app_log(parent_tag, "INFO", "Sending USER %s 0 * :%s", USER, REALNAME);
    send_irc(socket_fd, "USER %s 0 * :%s", USER, REALNAME);

    app_log(parent_tag, "INFO", "Registration messages sent. Waiting for MOTD end (376 or 422)...");

    fd_set read_fds;
    struct timeval tv;
    int motd_ended = 0;
    time_t registration_start_time = time(NULL);

    while (!motd_ended && !shutdown_requested && (time(NULL) - registration_start_time < 60)) { 
        FD_ZERO(&read_fds);
        FD_SET(socket_fd, &read_fds); 
        tv.tv_sec = 1; tv.tv_usec = 0;

        int activity = select(socket_fd + 1, &read_fds, NULL, NULL, &tv);
        if (activity < 0) {
            if (errno == EINTR && !shutdown_requested) continue; 
            app_log(parent_tag, "ERROR", "select error during registration: %s", strerror(errno));
            return EXIT_FAILURE;
        }
        if (activity == 0) continue; 

        if (FD_ISSET(socket_fd, &read_fds)) {
            clearBuffer(recv_buffer, buffer_size);
            ssize_t bytes_received = recv(socket_fd, recv_buffer, buffer_size - 1, 0); 
            if (bytes_received <= 0) {
                app_log(parent_tag, "ERROR", "Server disconnected or recv error during registration (bytes: %zd): %s", bytes_received, strerror(errno));
                return EXIT_FAILURE;
            }
            recv_buffer[bytes_received] = '\0';

            char *line_saveptr = NULL;
            char *line = strtok_r(recv_buffer, "\r\n", &line_saveptr);
            while (line != NULL) {
                app_log(parent_tag, "RECV_REG", "%s", line);
                if (strstr(line, " 376 ") != NULL || strstr(line, " 422 ") != NULL) { 
                    app_log(parent_tag, "INFO", "MOTD end / MOTD missing received. Registration complete.");
                    motd_ended = 1;
                    break; 
                }
                if (strncmp(line, "PING :", 6) == 0) {
                    send_irc(socket_fd, "PONG :%s", line + 6); 
                }
                line = strtok_r(NULL, "\r\n", &line_saveptr);
            }
        }
        if (motd_ended) break; 
    }

    if (!motd_ended) {
        app_log(parent_tag, "ERROR", "Timeout or failure waiting for MOTD end after %ld seconds.", (long)(time(NULL) - registration_start_time));
        return EXIT_FAILURE;
    }
    app_log(parent_tag, "INFO", "IRC Registration successful.");
    return EXIT_SUCCESS;
}

int ircJoinChannels(void) {
    char parent_tag[32];
    snprintf(parent_tag, sizeof(parent_tag), "Parent %d", getpid());
    app_log(parent_tag, "INFO", "Joining specified channels...");
    if (g_channel_infos == NULL || numChildren == 0) {
        app_log(parent_tag, "WARN", "No channels loaded to join.");
        return EXIT_SUCCESS; 
    }

    for (int i = 0; i < numChildren; i++) {
        if (g_channel_infos[i].name == NULL) continue; // Should not happen if loaded correctly

        if (shutdown_requested) {
            app_log(parent_tag, "WARN", "Shutdown requested during channel joining for %s.", g_channel_infos[i].name);
            return EXIT_FAILURE;
        }
        app_log(parent_tag, "INFO", "Attempting to JOIN %s", g_channel_infos[i].name);
        
        if (ADMIN_CHANNEL_NAME_CONST && strcmp(g_channel_infos[i].name, ADMIN_CHANNEL_NAME_CONST) == 0) {
            send_irc(socket_fd, "JOIN %s %s", g_channel_infos[i].name, ADMIN_CHANNEL_PASSWORD); 
            usleep(200000); 
            send_irc(socket_fd, "MODE %s +k %s", g_channel_infos[i].name, ADMIN_CHANNEL_PASSWORD); 
            app_log(parent_tag, "INFO", "Attempted to JOIN admin channel %s with password and set mode +k.", g_channel_infos[i].name);
        } else {
            send_irc(socket_fd, "JOIN %s", g_channel_infos[i].name); 
        }
        sleep(1); 
    }
    app_log(parent_tag, "INFO", "Finished attempting to join %d channels.", numChildren);
    return EXIT_SUCCESS;
}