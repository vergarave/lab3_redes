// broker_tcp.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

#define PORT 5000
#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024
#define TOPIC_SIZE 100

typedef enum {
    CLIENT_UNKNOWN,
    CLIENT_PUBLISHER,
    CLIENT_SUBSCRIBER
} ClientType;

typedef struct {
    int socket_fd;
    ClientType type;
    char topic[TOPIC_SIZE];
    int active;
} Client;

Client clients[MAX_CLIENTS];

void init_clients() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].socket_fd = -1;
        clients[i].type = CLIENT_UNKNOWN;
        clients[i].topic[0] = '\0';
        clients[i].active = 0;
    }
}

int add_client(int client_socket) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            clients[i].socket_fd = client_socket;
            clients[i].type = CLIENT_UNKNOWN;
            clients[i].topic[0] = '\0';
            clients[i].active = 1;
            return i;
        }
    }
    return -1;
}

void remove_client(int index) {
    if (index >= 0 && index < MAX_CLIENTS) {
        close(clients[index].socket_fd);
        clients[index].socket_fd = -1;
        clients[index].type = CLIENT_UNKNOWN;
        clients[index].topic[0] = '\0';
        clients[index].active = 0;
    }
}

void trim_newline(char *str) {
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r')) {
        str[len - 1] = '\0';
        len--;
    }
}

void send_to_subscribers(const char *topic, const char *message) {
    char final_message[BUFFER_SIZE + TOPIC_SIZE + 50];
    snprintf(final_message, sizeof(final_message), "[%s] %s\n", topic, message);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active &&
            clients[i].type == CLIENT_SUBSCRIBER &&
            strcmp(clients[i].topic, topic) == 0) {

            if (send(clients[i].socket_fd, final_message, strlen(final_message), 0) < 0) {
                perror("Error enviando a subscriber");
            }
        }
    }
}

int configure_client(int index, char *buffer) {
    trim_newline(buffer);

    char command[10];
    char topic[TOPIC_SIZE];

    if (sscanf(buffer, "%9s %99[^\n]", command, topic) != 2) {
        char *error_msg = "Formato invalido. Use: PUB <tema> o SUB <tema>\n";
        send(clients[index].socket_fd, error_msg, strlen(error_msg), 0);
        return 0;
    }

    if (strcmp(command, "PUB") == 0) {
        clients[index].type = CLIENT_PUBLISHER;
        strncpy(clients[index].topic, topic, TOPIC_SIZE - 1);
        clients[index].topic[TOPIC_SIZE - 1] = '\0';

        char response[200];
        snprintf(response, sizeof(response),"Registrado como publisher del tema: %s\n",clients[index].topic);
        send(clients[index].socket_fd, response, strlen(response), 0);
        return 1;
    }

    if (strcmp(command, "SUB") == 0) {
        clients[index].type = CLIENT_SUBSCRIBER;
        strncpy(clients[index].topic, topic, TOPIC_SIZE - 1);
        clients[index].topic[TOPIC_SIZE - 1] = '\0';

        char response[200];
        snprintf(response, sizeof(response),"Registrado como subscriber del tema: %s\n",clients[index].topic);
        send(clients[index].socket_fd, response, strlen(response), 0);
        return 1;
    }

    {
        char *error_msg = "Comando invalido. Use PUB o SUB\n";
        send(clients[index].socket_fd, error_msg, strlen(error_msg), 0);
        return 0;
    }
}

int main() {
    int server_fd, new_socket, max_sd, activity;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addrlen = sizeof(client_addr);
    fd_set readfds;
    char buffer[BUFFER_SIZE];

    init_clients();

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Error al crear socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error en bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("Error en listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Broker TCP escuchando en el puerto %d...\n", PORT);
    printf("Use:\n");
    printf("  SUB <tema>  para subscriber\n");
    printf("  PUB <tema>  para publisher\n\n");

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        max_sd = server_fd;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active) {
                FD_SET(clients[i].socket_fd, &readfds);
                if (clients[i].socket_fd > max_sd) {
                    max_sd = clients[i].socket_fd;
                }
            }
        }

        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

        if (activity < 0) {
            perror("Error en select");
            continue;
        }

        // Nueva conexión
        if (FD_ISSET(server_fd, &readfds)) {
            new_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
            if (new_socket < 0) {
                perror("Error en accept");
                continue;
            }

            int idx = add_client(new_socket);
            if (idx == -1) {
                char *msg = "Broker lleno. No se aceptan mas clientes.\n";
                send(new_socket, msg, strlen(msg), 0);
                close(new_socket);
            } else {
                printf("Nueva conexion aceptada. Socket=%d, IP=%s, Puerto=%d\n", new_socket, inet_ntoa(client_addr.sin_addr),ntohs(client_addr.sin_port));

                char *msg = "Bienvenido al broker TCP. Identifiquese con PUB <tema> o SUB <tema>\n";
                send(new_socket, msg, strlen(msg), 0);
            }
        }

        // Actividad de clientes
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active && FD_ISSET(clients[i].socket_fd, &readfds)) {
                memset(buffer, 0, BUFFER_SIZE);
                int bytes_read = recv(clients[i].socket_fd, buffer, BUFFER_SIZE - 1, 0);

                if (bytes_read <= 0) {
                    printf("Cliente desconectado. Socket=%d\n", clients[i].socket_fd);
                    remove_client(i);
                    continue;
                }

                buffer[bytes_read] = '\0';
                trim_newline(buffer);

                // Si aún no está configurado, primer mensaje define el rol
                if (clients[i].type == CLIENT_UNKNOWN) {
                    configure_client(i, buffer);
                    continue;
                }

                // Si es publisher, todo lo que mande se redistribuye
                if (clients[i].type == CLIENT_PUBLISHER) {
                    printf("Mensaje recibido de publisher [%s]: %s\n", clients[i].topic, buffer);

                    send_to_subscribers(clients[i].topic, buffer);
                } else {
                    // Si es subscriber y manda algo, opcionalmente ignoramos
                    char *msg = "Usted es subscriber. Solo recibe mensajes.\n";
                    send(clients[i].socket_fd, msg, strlen(msg), 0);
                }
            }
        }
    }

    close(server_fd);
    return 0;
}