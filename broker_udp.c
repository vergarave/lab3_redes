// broker_udp.c
// Broker para el sistema publicacion-suscripcion usando UDP.
// Recibe datagramas de publishers y los reenvía a subscribers registrados.
//
// Protocolo de registro (primer mensaje de cada cliente):
//   "PUB <tema>"  -> registra al remitente como publisher del tema
//   "SUB <tema>"  -> registra al remitente como subscriber del tema
//
// Cualquier mensaje posterior de un publisher se reenvía a todos los
// subscribers inscritos en el mismo tema.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT        5001
#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024
#define TOPIC_SIZE  100

typedef enum {
    CLIENT_UNKNOWN,
    CLIENT_PUBLISHER,
    CLIENT_SUBSCRIBER
} ClientType;

typedef struct {
    struct sockaddr_in addr;
    ClientType         type;
    char               topic[TOPIC_SIZE];
    int                active;
} Client;

Client clients[MAX_CLIENTS];

void init_clients(void) {
    memset(clients, 0, sizeof(clients));
}

void trim_newline(char *str) {
    size_t len = strlen(str);
    while (len > 0 && (str[len-1] == '\n' || str[len-1] == '\r' || str[len-1] == ' '))
        str[--len] = '\0';
}

int addr_equal(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return (a->sin_addr.s_addr == b->sin_addr.s_addr) &&
           (a->sin_port         == b->sin_port);
}

int find_client(const struct sockaddr_in *addr) {
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].active && addr_equal(&clients[i].addr, addr))
            return i;
    return -1;
}

int add_client(const struct sockaddr_in *addr) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            clients[i].addr     = *addr;
            clients[i].type     = CLIENT_UNKNOWN;
            clients[i].topic[0] = '\0';
            clients[i].active   = 1;
            return i;
        }
    }
    return -1;
}

void send_to_subscribers(int sockfd, const char *topic, const char *message) {
    char final_msg[BUFFER_SIZE + TOPIC_SIZE + 50];
    int  len = snprintf(final_msg, sizeof(final_msg), "[%s] %s\n", topic, message);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active &&
            clients[i].type == CLIENT_SUBSCRIBER &&
            strcmp(clients[i].topic, topic) == 0) {
            /*
             * sendto(): envia un datagrama UDP a la direccion del subscriber.
             * No hay conexion persistente; cada llamada especifica el destino.
             * Parametros:
             *   sockfd         - socket UDP del broker
             *   final_msg      - mensaje formateado [tema] contenido
             *   len            - bytes a enviar
             *   0              - flags (sin opciones especiales)
             *   &clients[i].addr - estructura sockaddr_in del subscriber
             *   sizeof(...)    - tamano de la estructura de direccion
             */
            if (sendto(sockfd, final_msg, len, 0,
                       (struct sockaddr *)&clients[i].addr,
                       sizeof(clients[i].addr)) < 0)
                perror("Error enviando a subscriber UDP");
        }
    }
}

void configure_client(int sockfd, int idx, const char *buffer) {
    char command[10], topic[TOPIC_SIZE], response[256];

    if (sscanf(buffer, "%9s %99[^\n]", command, topic) != 2) {
        const char *err = "Formato invalido. Use: PUB <tema> o SUB <tema>\n";
        sendto(sockfd, err, strlen(err), 0,
               (struct sockaddr *)&clients[idx].addr, sizeof(clients[idx].addr));
        return;
    }

    if (strcmp(command, "PUB") == 0) {
        clients[idx].type = CLIENT_PUBLISHER;
        strncpy(clients[idx].topic, topic, TOPIC_SIZE - 1);
        clients[idx].topic[TOPIC_SIZE - 1] = '\0';
        snprintf(response, sizeof(response),
                 "Registrado como publisher del tema: %s\n", topic);

    } else if (strcmp(command, "SUB") == 0) {
        clients[idx].type = CLIENT_SUBSCRIBER;
        strncpy(clients[idx].topic, topic, TOPIC_SIZE - 1);
        clients[idx].topic[TOPIC_SIZE - 1] = '\0';
        snprintf(response, sizeof(response),
                 "Registrado como subscriber del tema: %s\n", topic);

    } else {
        snprintf(response, sizeof(response), "Comando invalido. Use PUB o SUB\n");
    }

    sendto(sockfd, response, strlen(response), 0,
           (struct sockaddr *)&clients[idx].addr, sizeof(clients[idx].addr));
}

int main(void) {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addrlen = sizeof(client_addr);
    char buffer[BUFFER_SIZE];

    init_clients();

    /*
     * socket(): crea un socket UDP.
     *   AF_INET    - familia de direcciones IPv4
     *   SOCK_DGRAM - tipo datagrama (sin conexion) -> UDP
     *   0          - protocolo por defecto para DGRAM = UDP (IPPROTO_UDP)
     */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("Error al crear socket UDP"); exit(EXIT_FAILURE); }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(PORT);

    /*
     * bind(): vincula el socket a la direccion y puerto del broker.
     * A diferencia de TCP, con UDP no se llama a listen() ni accept();
     * el broker simplemente espera datagramas con recvfrom().
     */
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error en bind"); close(sockfd); exit(EXIT_FAILURE);
    }

    printf("Broker UDP escuchando en el puerto %d...\n", PORT);
    printf("Protocolo: PUB <tema> para publicar | SUB <tema> para suscribirse\n\n");

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);

        /*
         * recvfrom(): bloquea hasta recibir un datagrama UDP.
         *   buffer      - destino de los datos recibidos
         *   BUFFER_SIZE-1 - maximo de bytes a leer
         *   0           - flags
         *   &client_addr - se rellena con IP y puerto del remitente
         *   &addrlen    - tamano de la estructura de direccion (in/out)
         * Retorna: numero de bytes recibidos, o -1 si hay error.
         */
        int bytes = recvfrom(sockfd, buffer, BUFFER_SIZE - 1, 0,
                             (struct sockaddr *)&client_addr, &addrlen);
        if (bytes < 0) { perror("Error en recvfrom"); continue; }

        buffer[bytes] = '\0';
        trim_newline(buffer);

        int idx = find_client(&client_addr);
        if (idx == -1) {
            idx = add_client(&client_addr);
            if (idx == -1) {
                const char *full = "Broker lleno. No se aceptan mas clientes.\n";
                sendto(sockfd, full, strlen(full), 0,
                       (struct sockaddr *)&client_addr, addrlen);
                continue;
            }
            printf("Nuevo cliente UDP: %s:%d\n",
                   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            configure_client(sockfd, idx, buffer);
            continue;
        }

        if (clients[idx].type == CLIENT_UNKNOWN) {
            configure_client(sockfd, idx, buffer);
        } else if (clients[idx].type == CLIENT_PUBLISHER) {
            printf("Publisher [%s]: %s\n", clients[idx].topic, buffer);
            send_to_subscribers(sockfd, clients[idx].topic, buffer);
        } else {
            const char *msg = "Usted es subscriber. Solo recibe mensajes.\n";
            sendto(sockfd, msg, strlen(msg), 0,
                   (struct sockaddr *)&clients[idx].addr, sizeof(clients[idx].addr));
        }
    }

    close(sockfd);
    return 0;
}
