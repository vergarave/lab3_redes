// publisher_quic.c
// Publisher QUIC-like: UDP con ACKs y retransmision automatica.
//
// Protocolo:
//   Registro:    "PUB <tema>"
//   Publicacion: "MSG <seq> <contenido>"
//   Espera ACK:  "ACK <seq>" del broker
//   Si no llega ACK en TIMEOUT_MS milisegundos, retransmite hasta MAX_RETRIES.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>

#define PORT        5002
#define SERVER_IP   "127.0.0.1"
#define BUFFER_SIZE 1200
#define TOPIC_SIZE  100
#define TIMEOUT_SEC 1          // timeout para esperar ACK
#define MAX_RETRIES 5

void trim_newline(char *str) {
    size_t len = strlen(str);
    while (len > 0 && (str[len-1]=='\n'||str[len-1]=='\r'))
        str[--len] = '\0';
}

// Envia un mensaje MSG y espera ACK. Retransmite si no llega.
// Retorna 1 si el ACK fue recibido, 0 si se agotaron los intentos.
int send_with_ack(int sockfd, struct sockaddr_in *srv, socklen_t addrlen,
                  uint32_t seq, const char *msg_buf, int msg_len) {
    char ack_buf[64];
    uint32_t ack_seq;

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        // sendto(): envia el datagrama MSG al broker
        if (sendto(sockfd, msg_buf, msg_len, 0,
                   (struct sockaddr *)srv, addrlen) < 0) {
            perror("sendto"); return 0;
        }

        // Esperamos ACK con timeout usando select()
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sockfd, &rfds);
        struct timeval tv = { TIMEOUT_SEC, 0 };

        int ready = select(sockfd + 1, &rfds, NULL, NULL, &tv);
        if (ready <= 0) {
            printf("  [seq=%u] Sin ACK (intento %d/%d), retransmitiendo...\n",
                   seq, attempt, MAX_RETRIES);
            continue;
        }

        // Recibir respuesta
        memset(ack_buf, 0, sizeof(ack_buf));
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        int bytes = recvfrom(sockfd, ack_buf, sizeof(ack_buf)-1, 0,
                             (struct sockaddr *)&from, &flen);
        if (bytes <= 0) continue;
        ack_buf[bytes] = '\0';
        trim_newline(ack_buf);

        // Verificar que sea el ACK correcto
        if (sscanf(ack_buf, "ACK %u", &ack_seq) == 1 && ack_seq == seq) {
            printf("  [seq=%u] ACK recibido.\n", seq);
            return 1;
        }
    }

    printf("  [seq=%u] FALLO: no se recibio ACK tras %d intentos.\n",
           seq, MAX_RETRIES);
    return 0;
}

int main(void) {
    int sockfd;
    struct sockaddr_in server_addr;
    socklen_t addrlen = sizeof(server_addr);
    char buffer[BUFFER_SIZE], topic[TOPIC_SIZE];
    char msg_buf[BUFFER_SIZE];
    uint32_t seq = 1;   // numero de secuencia, incrementa con cada mensaje

    printf("=== PUBLISHER QUIC-like ===\n");
    printf("Caracteristicas: ACK por mensaje + retransmision automatica\n");
    printf("Ingrese el tema en el que va a publicar: ");
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

    // Registro: "PUB <tema>"
    snprintf(msg_buf, sizeof(msg_buf), "PUB %s\n", topic);
    sendto(sockfd, msg_buf, strlen(msg_buf), 0,
           (struct sockaddr *)&server_addr, addrlen);

    // Esperar confirmacion del broker
    struct sockaddr_in from;
    socklen_t flen = sizeof(from);
    int bytes = recvfrom(sockfd, buffer, sizeof(buffer)-1, 0,
                         (struct sockaddr *)&from, &flen);
    if (bytes > 0) { buffer[bytes] = '\0'; printf("%s", buffer); }

    printf("\nEscriba mensajes para [%s]. 'salir' para terminar.\n\n", topic);

    while (1) {
        printf("Mensaje (seq=%u): ", seq);
        if (!fgets(buffer, sizeof(buffer), stdin)) break;
        trim_newline(buffer);
        if (strlen(buffer) == 0) continue;
        if (strcmp(buffer, "salir") == 0) break;

        // Formato MSG: "MSG <seq> <contenido>"
        int mlen = snprintf(msg_buf, sizeof(msg_buf),
                            "MSG %u %s\n", seq, buffer);

        // Enviar con garantia de ACK y retransmision si es necesario
        send_with_ack(sockfd, &server_addr, addrlen, seq, msg_buf, mlen);
        seq++;
    }

    close(sockfd);
    printf("Publisher QUIC desconectado. Ultimo seq=%u\n", seq - 1);
    return 0;
}
