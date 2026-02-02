#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 12346
#define BUF_SIZE 1024

void error_exit(const char *msg) {
    perror(msg);
    exit(1);
}

int main() {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUF_SIZE];

    printf("=== CLIENT PISICILE EXPLOZIVE ===\n");

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        error_exit("Eroare la creare socket");

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0)
        error_exit("Adresa server invalida");

    printf("Conectare la server...\n");
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        error_exit("Conectare esuata");

    printf("Conectat la server.\n");
    printf("Introdu numele tau: ");
    memset(buffer, 0, BUF_SIZE);
    fgets(buffer, BUF_SIZE, stdin);
    buffer[strcspn(buffer, "\n")] = 0;
    send(sock, buffer, strlen(buffer), 0);
 
    printf("Conectat la server. Asteptam ceilalti jucatori...\n");

    while (1) {
        memset(buffer, 0, BUF_SIZE);
        int len = recv(sock, buffer, BUF_SIZE - 1, 0);
        if (len <= 0) {
            printf("Serverul a inchis conexiunea.\n");
            break;
        }

        printf("%s", buffer);

        if (strstr(buffer, "[INPUT]")) {
            printf(">> ");
            memset(buffer, 0, BUF_SIZE);
            fgets(buffer, BUF_SIZE, stdin);
            buffer[strcspn(buffer, "\n")] = 0;
            send(sock, buffer, strlen(buffer), 0);
        }
    }

    close(sock);
    return 0;
}
