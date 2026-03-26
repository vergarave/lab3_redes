// subscriber_tcp.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 5000
#define SERVER_IP "127.0.0.1"
#define BUFFER_SIZE 1024
#define TOPIC_SIZE 100

void trim_newline(char *str) {
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r')) {
        str[len - 1] = '\0';
        len--;
    }
}

int main() {
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char topic[TOPIC_SIZE];
    char subscribe_msg[TOPIC_SIZE + 10];

    printf("=== SUBSCRIBER TCP ===\n");
    printf("Ingrese el tema al que se quiere suscribir: ");
    if (fgets(topic, sizeof(topic), stdin) == NULL) {
        printf("Error leyendo el tema.\n");
        return 1;
    }

    trim_newline(topic);

    if (strlen(topic) == 0) {
        printf("El tema no puede estar vacio.\n");
        return 1;
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Error al crear socket");
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Direccion IP invalida");
        close(sockfd);
        return 1;
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error al conectar con el broker");
        close(sockfd);
        return 1;
    }

    printf("Conectado al broker en %s:%d\n", SERVER_IP, PORT);

    // Leer mensaje inicial del broker
    memset(buffer, 0, sizeof(buffer));
    int bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        printf("%s", buffer);
    }

    // Enviar suscripción
    snprintf(subscribe_msg, sizeof(subscribe_msg), "SUB %s\n", topic);

    if (send(sockfd, subscribe_msg, strlen(subscribe_msg), 0) < 0) {
        perror("Error enviando suscripcion");
        close(sockfd);
        return 1;
    }

    // Leer confirmación
    memset(buffer, 0, sizeof(buffer));
    bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        printf("%s", buffer);
    }

    printf("Esperando mensajes del tema [%s]...\n\n", topic);

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);

        if (bytes <= 0) {
            printf("Conexion cerrada por el broker.\n");
            break;
        }

        buffer[bytes] = '\0';
        printf("%s", buffer);
        fflush(stdout);
    }

    close(sockfd);
    return 0;
}