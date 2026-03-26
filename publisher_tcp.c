// publisher_tcp.c
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
    char register_msg[TOPIC_SIZE + 10];

    printf("=== PUBLISHER TCP ===\n");
    printf("Ingrese el tema en el que va a publicar: ");
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

    // Registrarse como publisher
    snprintf(register_msg, sizeof(register_msg), "PUB %s\n", topic);

    if (send(sockfd, register_msg, strlen(register_msg), 0) < 0) {
        perror("Error enviando registro de publisher");
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

    printf("\nEscriba mensajes para el tema [%s]\n", topic);
    printf("Escriba 'salir' para terminar.\n\n");

    while (1) {
        printf("Mensaje: ");
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            printf("\nFin de entrada.\n");
            break;
        }

        trim_newline(buffer);

        if (strlen(buffer) == 0) {
            continue;
        }

        if (strcmp(buffer, "salir") == 0) {
            break;
        }

        if (send(sockfd, buffer, strlen(buffer), 0) < 0) {
            perror("Error enviando mensaje al broker");
            break;
        }
    }

    close(sockfd);
    return 0;
}