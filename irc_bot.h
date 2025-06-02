#ifndef IRC_BOT_H
#define IRC_BOT_H

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <sys/mman.h>
#include <netdb.h>
#include <semaphore.h>
#include <time.h>
#include <fcntl.h>
#include <stdbool.h>
#include <ctype.h>
#include <curl/curl.h>
#include "cJSON.h"    

// --- IRC CONFIG ---
#define SERVER_IP "10.1.0.46"
#define SERVER_PORT 6667
#define NICK "bdoke1272"
#define USER "bdoke1272"
#define REALNAME "bdoke1272"
#define PING_INTERVAL_SECONDS 15
#define QUIT_MESSAGE "Leaving now! Goodbyeeee..."

extern const char *ADMIN_CHANNEL_NAME_CONST;
#define ADMIN_CHANNEL_PASSWORD "mysecretpassword"

#define MAX_CHANNEL_NAME_LEN 64
#define MAX_PERSONA_LEN 256
#define MAX_NICK_LEN 32
#define PIPE_MSG_DELIMITER_CHAR '\t'
#define PIPE_MSG_DELIMITER_STR "\t"
#define MAX_PIPE_MSG_LEN 512 

#define LOG_FILE_PATH "irc_chat.log"
#define MUTED_USERS_FILE_PATH "muted_users.txt"

// --- Structure for Channel Info ---
typedef struct {
    char *name;
    char *persona; // Persona for the AI in this channel
} ChannelInfo;


// --- Global Variables (declared as extern) ---
extern volatile sig_atomic_t shutdown_requested;
extern volatile sig_atomic_t child_exit_flag;

extern int socket_fd;
extern pid_t pinger_child_pid;
extern int *worker_write_pipe_fds;
extern pid_t *worker_child_pids;
extern int numChildren; // Total number of channels (admin + workers)
extern int numWorkerChildren; // Number of worker channels (numChildren - 1 if admin channel exists)

extern sem_t *socket_lock;

// extern char **CHANNELS; // Replaced by array of ChannelInfo
extern ChannelInfo *g_channel_infos; // Array of ChannelInfo structs

extern char **g_muted_nicks;
extern int g_num_muted_users;

// --- Function Prototypes ---

// From utils.c
void clearBuffer(char buffer[], size_t size);
bool is_other_bot_nick(const char *nick);
int load_channels_from_file(const char *filename); // Modified to load personas
void free_channels_memory(void); // Modified
int load_muted_users_from_file(const char *filename);
void free_muted_users_memory(void);
bool is_user_globally_muted(const char *nick);
void app_log(const char *process_tag, const char *level, const char *format, ...); // Modified for dual logging
int add_muted_user(const char *nick);
int remove_muted_user(const char *nick);
int save_muted_users_to_file(const char *filename);

// From irc_network.c
void send_irc(int sock_param, const char *fmt, ...);
int initSocket(const char *server_ip, int server_port);
int ircRegister(char recv_buffer[], size_t buffer_size);
int ircJoinChannels(void);

// From child_processes.c
void child_PING(int sock_param, const char* ip_override);
void child_WORKER(int worker_id, const ChannelInfo* channel_info, int local_socket_fd, int pipe_read_fd); // Takes ChannelInfo
int forkChildren(const char *ip_override);

// From irc_core.c
void SIG_parent_handler(int numSignal);
void SIG_child_handler(int numSignal);
int initSIGNALS(void);
int initSemaphores(void);
void cleanup_semaphore(void);
void mainLoop(char recv_buffer[], size_t recv_buffer_size, int *child_status);
void softShutdown(int *child_status);

#endif // IRC_BOT_H