// subscriber_quic.c
// Subscriber QUIC-like: UDP con ACKs al broker y deteccion de mensajes fuera de orden.
//
// Protocolo:
//   Registro:    "SUB <tema>"
//   Recepcion:   "MSG <seq> [tema] contenido"
//   ACK enviado: "ACK <seq>" al broker tras cada mensaje recibido
//   Orden:       detecta y reporta mensajes fuera de secuencia

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>

#define PORT        5002
#define SERVER_IP   "127.0.0.1"
#define BUFFER_SIZE 1200
#define TOPIC_SIZE  100

void trim_newline(char *str) {
    size_t len = strlen(str);
    while (len > 0 && (str[len-1]=='\n'||str[len-1]=='\r'))
        str[--len] = '\0';
}

int main(void) {
    int sockfd;
    struct sockaddr_in server_addr, from_addr;
    socklen_t addrlen = sizeof(server_addr);
    socklen_t from_len = sizeof(from_addr);
    char buffer[BUFFER_SIZE], topic[TOPIC_SIZE];
    char ack_buf[64];
    uint32_t expected_seq = 1;  // siguiente seq esperado (para detectar orden)
    int total_received = 0, out_of_order = 0;

    printf("=== SUBSCRIBER QUIC-like ===\n");
    printf("Caracteristicas: ACK por mensaje + deteccion de orden\n");
    printf("Ingrese el tema al que se quiere suscribir: ");
    if (!fgets(topic, sizeof(topic), stdin)) return 1;
    trim_newline(topic);
    if (strlen(topic) == 0) { printf("Tema vacio.\n"); return 1; }

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("socket"); return 1; }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("IP invalida"); close(sockfd); return 1;
    }

    // Registro: "SUB <tema>"
    snprintf(buffer, sizeof(buffer), "SUB %s\n", topic);
    sendto(sockfd, buffer, strlen(buffer), 0,
           (struct sockaddr *)&server_addr, addrlen);

    // Confirmacion del broker
    int bytes = recvfrom(sockfd, buffer, sizeof(buffer)-1, 0,
                         (struct sockaddr *)&from_addr, &from_len);
    if (bytes > 0) { buffer[bytes] = '\0'; printf("%s", buffer); }

    printf("Esperando mensajes del tema [%s]...\n\n", topic);

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        bytes = recvfrom(sockfd, buffer, sizeof(buffer)-1, 0,
                         (struct sockaddr *)&from_addr, &from_len);
        if (bytes < 0) { perror("recvfrom"); break; }
        buffer[bytes] = '\0';
        trim_newline(buffer);

        uint32_t seq;
        char content[BUFFER_SIZE];

        // Parsear formato: "MSG <seq> <contenido>"
        if (sscanf(buffer, "MSG %u %[^\n]", &seq, content) == 2) {
            total_received++;

            // Verificar orden de secuencia
            if (seq != expected_seq) {
                out_of_order++;
                printf("[AVISO] Mensaje fuera de orden: recibido seq=%u, "
                       "esperado seq=%u\n", seq, expected_seq);
            }
            expected_seq = seq + 1;

            // Mostrar mensaje con timestamp
            time_t t = time(NULL);
            struct tm *tm_info = localtime(&t);
            char ts[20];
            strftime(ts, sizeof(ts), "%H:%M:%S", tm_info);
            printf("[%s][seq=%u] %s\n", ts, seq, content);
            fflush(stdout);

            // Enviar ACK al broker
            // El broker usa este ACK para saber que el subscriber recibio el mensaje
            // y dejar de retransmitir.
            int alen = snprintf(ack_buf, sizeof(ack_buf), "ACK %u\n", seq);
            sendto(sockfd, ack_buf, alen, 0,
                   (struct sockaddr *)&from_addr, from_len);
        }
        // Mensajes de control del broker (OK, ERR) se muestran directamente
        else if (strncmp(buffer, "OK", 2) == 0 || strncmp(buffer, "ERR", 3) == 0) {
            printf("%s\n", buffer);
        }
    }

    printf("\n--- Estadisticas ---\n");
    printf("Mensajes recibidos: %d\n", total_received);
    printf("Fuera de orden:     %d\n", out_of_order);

    close(sockfd);
    return 0;
}
