// subscriber_udp.c
// Suscriptor UDP para el sistema de noticias deportivas.
// Se registra en el broker y espera datagramas con actualizaciones del partido.
//
// Uso:
//   1. El usuario ingresa el tema al que se quiere suscribir
//   2. Se registra con "SUB <tema>" en el broker
//   3. Queda en escucha recibiendo mensajes del broker

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT        5001
#define SERVER_IP   "127.0.0.1"
#define BUFFER_SIZE 1024
#define TOPIC_SIZE  100

void trim_newline(char *str) {
    size_t len = strlen(str);
    while (len > 0 && (str[len-1] == '\n' || str[len-1] == '\r'))
        str[--len] = '\0';
}

int main(void) {
    int sockfd;
    struct sockaddr_in server_addr, from_addr;
    socklen_t addrlen = sizeof(server_addr);
    socklen_t from_len = sizeof(from_addr);
    char buffer[BUFFER_SIZE];
    char topic[TOPIC_SIZE];
    char subscribe_msg[TOPIC_SIZE + 10];

    printf("=== SUBSCRIBER UDP ===\n");
    printf("Ingrese el tema al que se quiere suscribir: ");
    if (!fgets(topic, sizeof(topic), stdin)) return 1;
    trim_newline(topic);
    if (strlen(topic) == 0) { printf("Tema vacio.\n"); return 1; }

    /*
     * socket(): crea socket UDP.
     *   AF_INET    - IPv4
     *   SOCK_DGRAM - datagrama sin conexion (UDP)
     *   0          - protocolo por defecto = UDP
     */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("Error al crear socket"); return 1; }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("IP invalida"); close(sockfd); return 1;
    }

    // Paso 1: registrarse como subscriber enviando "SUB <tema>"
    snprintf(subscribe_msg, sizeof(subscribe_msg), "SUB %s\n", topic);

    /*
     * sendto(): envia el mensaje de registro al broker.
     * No existe handshake en UDP; simplemente enviamos el datagrama.
     * Si se pierde, el broker nunca nos registra y no recibiremos mensajes.
     */
    if (sendto(sockfd, subscribe_msg, strlen(subscribe_msg), 0,
               (struct sockaddr *)&server_addr, addrlen) < 0) {
        perror("Error enviando suscripcion"); close(sockfd); return 1;
    }

    // Paso 2: esperar confirmacion
    memset(buffer, 0, sizeof(buffer));
    int bytes = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                         (struct sockaddr *)&from_addr, &from_len);
    if (bytes > 0) { buffer[bytes] = '\0'; printf("%s", buffer); }

    printf("Esperando mensajes del tema [%s]...\n\n", topic);

    // Paso 3: bucle de recepcion
    while (1) {
        memset(buffer, 0, sizeof(buffer));

        /*
         * recvfrom(): bloquea hasta recibir un datagrama.
         * En UDP:
         *   - Cada llamada recibe exactamente un datagrama.
         *   - Los datagramas pueden llegar desordenados.
         *   - Los datagramas pueden perderse sin notificacion.
         *   - No hay control de flujo ni retransmision automatica.
         * from_addr se rellena con la IP y puerto del remitente (el broker).
         */
        bytes = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                         (struct sockaddr *)&from_addr, &from_len);
        if (bytes < 0) {
            perror("Error en recvfrom");
            break;
        }

        buffer[bytes] = '\0';
        printf("%s", buffer);
        fflush(stdout);
    }

    close(sockfd);
    return 0;
}
