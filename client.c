#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BUFFER_SIZE 2048

void error(const char *msg) {
    perror(msg);
    exit(1);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <IP> <port>\n", argv[0]);
        exit(1);
    }

    int sockfd, portno;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];

    portno = atoi(argv[2]);
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("ERROR opening socket");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(portno);
    if (inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) <= 0)
        error("ERROR invalid address");

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR connecting");

    printf("Connected to server.\n");

    int quitted = 1;
    while (quitted) {
        memset(buffer, 0, BUFFER_SIZE);
        int n = read(sockfd, buffer, BUFFER_SIZE - 1);
        if (n <= 0) {
            printf("Server closed the connection.\n");
            break;
        }
        printf("Server: %s", buffer);

        if (strstr(buffer, "WAIT")){
            printf("Enter message: ");
            fgets(buffer, BUFFER_SIZE - 1, stdin);
            buffer[strcspn(buffer, "\n")] = 0;

            if (strcmp(buffer, "QUIT") == 0) {
                printf("Closing connection...\n");
                quitted = 0;
            }

            n = write(sockfd, buffer, strlen(buffer));
            if (n < 0) error("ERROR writing to socket");
        }
    }

    close(sockfd);
    return 0;
}