#include "irc_bot.h"
#include "gemini_integration.h"

// Global Variables
volatile sig_atomic_t shutdown_requested = 0;
volatile sig_atomic_t child_exit_flag = 0;
int socket_fd = -1;
pid_t pinger_child_pid = -1;
int *worker_write_pipe_fds = NULL;
pid_t *worker_child_pids = NULL;
int numChildren = 0;
int numWorkerChildren = 0;
sem_t *socket_lock = NULL;
ChannelInfo *g_channel_infos = NULL;

const char *ADMIN_CHANNEL_NAME_CONST = NULL;
char **g_muted_nicks = NULL;
int g_num_muted_users = 0;
const char *g_gemini_api_key = NULL; 

int main(int argc, char *argv[]) {
    char recv_buffer[2048];
    int child_status = 0;
    char parent_tag[32];
    const char *server_ip = NULL;
    const char *server_port = NULL;

    snprintf(parent_tag, sizeof(parent_tag), "Parent %d", getpid());

    // Parse command line arguments for IP and port
    if (argc >= 3) {
        server_ip = argv[1];
        server_port = argv[2];
    } else {
        app_log(parent_tag, "FATAL", "Usage: %s <server_ip> <server_port>", argv[0]);
        return EXIT_FAILURE;
    }

    app_log(parent_tag, "INFO", "Application starting...");
    app_log(parent_tag, "INFO", "Using server IP: %s, port: %s", server_ip, server_port);

    // Load Gemini API Key from environment variable ---
    g_gemini_api_key = getenv("GEMINIAI_API_KEY"); // Look for GEMINI_API_KEY
    if (g_gemini_api_key == NULL || strlen(g_gemini_api_key) == 0) {
        app_log(parent_tag, "WARN", "GEMINIAI_API_KEY environment variable not set. AI features will be disabled.");
        g_gemini_api_key = NULL; // Explicitly set to NULL for clarity
    } else {
        app_log(parent_tag, "INFO", "Gemini API Key loaded from environment.");
    }

    CURLcode curl_res = curl_global_init(CURL_GLOBAL_ALL);
    if (curl_res != CURLE_OK) {
        app_log(parent_tag, "FATAL", "curl_global_init() failed: %s", curl_easy_strerror(curl_res));
        return EXIT_FAILURE;
    }

    if (load_channels_from_file("channels.txt") <= 0) {
        app_log(parent_tag, "FATAL", "Could not load channels from file. Exiting.");
        goto cleanup_curl_global; // Jump to cleanup curl if channel loading fails
    }
    if (load_muted_users_from_file(MUTED_USERS_FILE_PATH) < 0) {
        app_log(parent_tag, "WARN", "Error loading muted users from %s. Starting with no users muted.", MUTED_USERS_FILE_PATH);
    }

    if (initSIGNALS() != EXIT_SUCCESS) {
        app_log(parent_tag, "FATAL", "Signal initialization failed. Exiting.");
        goto cleanup_before_init_socket;
    }
    if (initSemaphores() != EXIT_SUCCESS) {
        app_log(parent_tag, "FATAL", "Semaphore initialization failed. Exiting.");
        goto cleanup_before_init_socket;
    }
    int server_port_num = atoi(server_port);
    if (initSocket(server_ip, server_port_num) != EXIT_SUCCESS) {
        app_log(parent_tag, "FATAL", "Socket connection failed. Exiting.");
        goto cleanup_before_init_socket;
    }

    if (ircRegister(recv_buffer, sizeof(recv_buffer)) != EXIT_SUCCESS) {
        app_log(parent_tag, "FATAL", "IRC registration failed. Exiting.");
        goto cleanup_after_socket_init;
    }

    if (ircJoinChannels() != EXIT_SUCCESS) {
        app_log(parent_tag, "WARN", "Failed during IRC channel joining. Problems may occur.");
    }

    if (forkChildren(server_ip) != EXIT_SUCCESS) {
        app_log(parent_tag, "WARN", "Failed to fork all child processes. Functionality will be limited.");
    }

    if (!shutdown_requested) {
         app_log(parent_tag, "INFO", "Entering main processing loop...");
         mainLoop(recv_buffer, sizeof(recv_buffer), &child_status);
         app_log(parent_tag, "INFO", "Exited main processing loop.");
    } else {
        app_log(parent_tag, "INFO", "Shutdown requested before main loop. Proceeding to shutdown.");
    }

cleanup_after_socket_init:
cleanup_before_init_socket: 
    free_channels_memory();
    free_muted_users_memory();
    cleanup_semaphore();
cleanup_curl_global:
    curl_global_cleanup();

    softShutdown(&child_status); // Ensures children and pinger are handled, and socket closed if open
    app_log(parent_tag, "INFO", "Application exiting.");
    return EXIT_SUCCESS;
}