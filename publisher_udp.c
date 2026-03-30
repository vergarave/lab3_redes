// publisher_udp.c
// Publicador UDP para el sistema de noticias deportivas.
// Envía datagramas al broker, que los redistribuye a los subscribers.
//
// Uso:
//   1. El usuario ingresa el tema (ej. "Colombia vs Brasil")
//   2. Se registra con "PUB <tema>" en el broker
//   3. Escribe mensajes que se envían como datagramas individuales
//   4. Escribe "salir" para terminar

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
    struct sockaddr_in server_addr;
    socklen_t addrlen = sizeof(server_addr);
    char buffer[BUFFER_SIZE];
    char topic[TOPIC_SIZE];
    char register_msg[TOPIC_SIZE + 10];

    printf("=== PUBLISHER UDP ===\n");
    printf("Ingrese el tema en el que va a publicar: ");
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

    // Paso 1: registrarse como publisher enviando "PUB <tema>"
    snprintf(register_msg, sizeof(register_msg), "PUB %s\n", topic);

    /*
     * sendto(): envia el datagrama de registro al broker.
     * No hay conexion previa (a diferencia de TCP con connect()).
     * Cada llamada a sendto especifica el destino explicitamente.
     *   register_msg  - mensaje de registro
     *   strlen(...)   - longitud del mensaje
     *   0             - flags
     *   &server_addr  - direccion del broker
     *   addrlen       - tamano de la estructura
     */
    if (sendto(sockfd, register_msg, strlen(register_msg), 0,
               (struct sockaddr *)&server_addr, addrlen) < 0) {
        perror("Error enviando registro"); close(sockfd); return 1;
    }

    // Paso 2: esperar confirmacion del broker
    memset(buffer, 0, sizeof(buffer));
    /*
     * recvfrom(): recibe la respuesta del broker.
     * En UDP, cada llamada devuelve exactamente un datagrama.
     * Si el datagrama se pierde, esta llamada bloquea indefinidamente
     * (no hay retransmision automatica como en TCP).
     */
    int bytes = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                         (struct sockaddr *)&server_addr, &addrlen);
    if (bytes > 0) { buffer[bytes] = '\0'; printf("%s", buffer); }

    printf("\nEscriba mensajes para el tema [%s]\n", topic);
    printf("Escriba 'salir' para terminar.\n\n");

    // Paso 3: bucle de publicacion
    while (1) {
        printf("Mensaje: ");
        if (!fgets(buffer, sizeof(buffer), stdin)) break;
        trim_newline(buffer);
        if (strlen(buffer) == 0) continue;
        if (strcmp(buffer, "salir") == 0) break;

        /*
         * sendto(): envia cada mensaje como un datagrama independiente.
         * UDP no garantiza entrega ni orden; si el paquete se pierde,
         * el broker nunca lo recibe y no hay notificacion al publisher.
         */
        if (sendto(sockfd, buffer, strlen(buffer), 0,
                   (struct sockaddr *)&server_addr, addrlen) < 0) {
            perror("Error enviando mensaje"); break;
        }
    }

    close(sockfd);
    printf("Publisher UDP desconectado.\n");
    return 0;
}
