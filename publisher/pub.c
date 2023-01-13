/*
 *      File: pub.c
 *      Authors: Gonçalo Sampaio Bárias (ist1103124)
 *               Pedro Perez Vieira (ist1100064)
 *      Description: Program that publishes information into the server boxes.
 */

#include "pub.h"
#include "../protocol/protocol.h"
#include "../utils/logging.h"
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

volatile sig_atomic_t shutdown_signaler = 0;

static void print_usage() {
    fprintf(stderr, "Usage: pub <register_pipe_name> <pipe_name> <box_name>\n");
}

int main(int argc, char **argv) {
    if (argc != 3) {
        print_usage();
        return EXIT_FAILURE;
    }

    signal(SIGINT, publisher_shutdown);
    signal(SIGPIPE, publisher_shutdown);

    // Session pipe name is truncated to fit the request message
    char session_pipename[CLIENT_NAMED_PIPE_MAX_LEN] = {0};
    strncpy(session_pipename, argv[2], CLIENT_NAMED_PIPE_MAX_LEN - 1);

    // Creates the session pipename for the publisher
    if (mkfifo(session_pipename, 0777) < 0) {
        WARN("Failed to create session pipe");
        return EXIT_FAILURE;
    }

    // Requests the mbroker for a connection
    if (client_request_connection(argv[1], SERVER_CODE_PUB_REGISTER,
                                  session_pipename, argv[3]) != 0) {
        unlink(session_pipename);
        return EXIT_FAILURE;
    }

    // Writes messages from the stdin into the mbroker
    if (publisher_write_messages(session_pipename) != 0) {
        unlink(session_pipename);
        return EXIT_FAILURE;
    }

    unlink(session_pipename);
    return 0;
}

int publisher_write_messages(char *session_pipename) {
    size_t packet_len = sizeof(uint8_t) + sizeof(char) * MSG_MAX_LEN;
    packet_ensure_len_limit(packet_len);
    int8_t packet[packet_len];

    // [ code = 9 (uint8_t) ] | [ message (char[1024]) ]
    int session_pipe_in = open(session_pipename, O_WRONLY);
    if (session_pipe_in < 0) {
        WARN("Failed to open pipe");
        return -1;
    }

    uint8_t code = SERVER_CODE_MESSAGE_RECEIVE;
    char message[MSG_MAX_LEN] = {0};
    char c = '\0';
    while (c != EOF && shutdown_signaler == 0) {
        // Reads a full message of MSG_MAX_LEN length at a time until it reaches
        // \n or EOF. Doesn't truncate the message.
        int message_len = 0;
        while (message_len < MSG_MAX_LEN && (c = getchar()) != '\n' &&
               c != EOF) {
            message[message_len++] = c;
        }
        message[message_len] = '\0';

        size_t packet_offset = 0;
        memset(packet, 0, packet_len);
        packet_write(packet, &packet_offset, &code, sizeof(uint8_t));
        packet_write(packet, &packet_offset, message,
                     sizeof(char) * strlen(message));
        if (pipe_write(session_pipe_in, packet, packet_len) != 0) {
            close(session_pipe_in);
            return -1;
        }
    }
    close(session_pipe_in);

    return 0;
}

void publisher_shutdown(int signum) { shutdown_signaler = signum; }
