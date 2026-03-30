// broker_quic.c
// Broker QUIC-like: UDP con ACKs, retransmision y numeros de secuencia.
//
// Protocolo de mensajes (todos en texto plano):
//   Registro:    "PUB <tema>"  /  "SUB <tema>"
//   Publicacion: "MSG <seq> <tema> <contenido>"
//   ACK:         "ACK <seq>"
//   Respuesta:   "OK <texto>" o "ERR <texto>"
//
// El broker:
//   1. Recibe mensajes MSG con numero de secuencia del publisher.
//   2. Envia ACK al publisher confirmando recepcion.
//   3. Reenvía el mensaje a los subscribers del tema.
//   4. Los subscribers envian ACK al broker por cada mensaje recibido.
//   5. Si el broker no recibe ACK de un subscriber en TIMEOUT_US microsegundos,
//      retransmite hasta MAX_RETRIES veces.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <time.h>
#include <errno.h>

#define PORT          5002
#define MAX_CLIENTS   100
#define BUFFER_SIZE   1200
#define TOPIC_SIZE    100
#define MAX_PENDING   200       // mensajes pendientes de ACK de subscribers
#define TIMEOUT_US    500000    // 500 ms antes de retransmitir
#define MAX_RETRIES   5         // intentos maximos por mensaje

typedef enum { CLIENT_UNKNOWN, CLIENT_PUBLISHER, CLIENT_SUBSCRIBER } ClientType;

typedef struct {
    struct sockaddr_in addr;
    ClientType         type;
    char               topic[TOPIC_SIZE];
    int                active;
} Client;

// Mensaje pendiente de ACK de un subscriber
typedef struct {
    int    active;
    int    sub_idx;            // indice del subscriber en clients[]
    uint32_t seq;              // numero de secuencia original del publisher
    char   payload[BUFFER_SIZE]; // "[tema] contenido"
    int    payload_len;
    int    retries;
    struct timespec last_sent; // momento del ultimo envio
} PendingACK;

Client     clients[MAX_CLIENTS];
PendingACK pending[MAX_PENDING];
int        sockfd;

// ---------- Utilidades ----------

void trim_newline(char *str) {
    size_t len = strlen(str);
    while (len > 0 && (str[len-1]=='\n'||str[len-1]=='\r'||str[len-1]==' '))
        str[--len] = '\0';
}

int addr_equal(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

int find_client(const struct sockaddr_in *addr) {
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].active && addr_equal(&clients[i].addr, addr)) return i;
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

// Diferencia en microsegundos entre dos timespec
long diff_us(struct timespec *a, struct timespec *b) {
    return (b->tv_sec - a->tv_sec) * 1000000L +
           (b->tv_nsec - a->tv_nsec) / 1000L;
}

// Envia un datagrama al cliente idx
void send_to(int idx, const char *msg, int len) {
    sendto(sockfd, msg, len, 0,
           (struct sockaddr *)&clients[idx].addr, sizeof(clients[idx].addr));
}

// ---------- Gestion de pendientes ----------

// Agrega un mensaje a la cola de pendientes para un subscriber
void add_pending(int sub_idx, uint32_t seq, const char *payload, int plen) {
    for (int i = 0; i < MAX_PENDING; i++) {
        if (!pending[i].active) {
            pending[i].active      = 1;
            pending[i].sub_idx     = sub_idx;
            pending[i].seq         = seq;
            pending[i].retries     = 0;
            strncpy(pending[i].payload, payload, BUFFER_SIZE - 1);
            pending[i].payload[BUFFER_SIZE - 1] = '\0';
            pending[i].payload_len = plen;
            clock_gettime(CLOCK_MONOTONIC, &pending[i].last_sent);
            return;
        }
    }
    fprintf(stderr, "Cola de pendientes llena!\n");
}

// Confirma un ACK recibido de un subscriber para la secuencia seq
void confirm_ack(int sub_idx, uint32_t seq) {
    for (int i = 0; i < MAX_PENDING; i++) {
        if (pending[i].active &&
            pending[i].sub_idx == sub_idx &&
            pending[i].seq     == seq) {
            pending[i].active = 0;
            printf("  ACK confirmado: subscriber %d seq=%u\n", sub_idx, seq);
            return;
        }
    }
}

// Revisa pendientes y retransmite los que han expirado
void check_retransmissions(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    for (int i = 0; i < MAX_PENDING; i++) {
        if (!pending[i].active) continue;
        if (diff_us(&pending[i].last_sent, &now) < TIMEOUT_US) continue;

        if (pending[i].retries >= MAX_RETRIES) {
            printf("  Descartando mensaje seq=%u para subscriber %d "
                   "(max reintentos)\n", pending[i].seq, pending[i].sub_idx);
            pending[i].active = 0;
            continue;
        }

        // Retransmitir
        pending[i].retries++;
        clock_gettime(CLOCK_MONOTONIC, &pending[i].last_sent);
        send_to(pending[i].sub_idx,
                pending[i].payload, pending[i].payload_len);
        printf("  Retransmitiendo seq=%u a subscriber %d (intento %d)\n",
               pending[i].seq, pending[i].sub_idx, pending[i].retries);
    }
}

// ---------- Logica del broker ----------

void configure_client(int idx, const char *buffer) {
    char command[10], topic[TOPIC_SIZE], response[256];

    if (sscanf(buffer, "%9s %99[^\n]", command, topic) != 2) {
        const char *err = "ERR Formato invalido. Use: PUB <tema> o SUB <tema>\n";
        send_to(idx, err, strlen(err));
        return;
    }

    if (strcmp(command, "PUB") == 0) {
        clients[idx].type = CLIENT_PUBLISHER;
        strncpy(clients[idx].topic, topic, TOPIC_SIZE-1);
        snprintf(response, sizeof(response),
                 "OK Registrado como publisher del tema: %s\n", topic);
    } else if (strcmp(command, "SUB") == 0) {
        clients[idx].type = CLIENT_SUBSCRIBER;
        strncpy(clients[idx].topic, topic, TOPIC_SIZE-1);
        snprintf(response, sizeof(response),
                 "OK Registrado como subscriber del tema: %s\n", topic);
    } else {
        snprintf(response, sizeof(response), "ERR Comando invalido\n");
    }
    send_to(idx, response, strlen(response));
}

// Distribuye un mensaje MSG a los subscribers y acola ACKs pendientes
void distribute_message(int pub_idx, uint32_t seq,
                        const char *topic, const char *content) {
    char payload[BUFFER_SIZE];
    // Formato que recibe el subscriber: "MSG <seq> [tema] contenido"
    int plen = snprintf(payload, sizeof(payload),
                        "MSG %u [%s] %s\n", seq, topic, content);

    int sent = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active &&
            clients[i].type == CLIENT_SUBSCRIBER &&
            strcmp(clients[i].topic, topic) == 0) {
            send_to(i, payload, plen);
            add_pending(i, seq, payload, plen);
            sent++;
        }
    }

    // ACK al publisher confirmando que el broker recibio el mensaje
    char ack[64];
    int alen = snprintf(ack, sizeof(ack), "ACK %u\n", seq);
    send_to(pub_idx, ack, alen);
    printf("Publisher [%s] seq=%u -> enviado a %d subscriber(s)\n",
           topic, seq, sent);
}

void handle_message(int idx, const char *buffer) {
    // Formato: "MSG <seq> <contenido>"
    uint32_t seq;
    char content[BUFFER_SIZE];

    if (sscanf(buffer, "MSG %u %[^\n]", &seq, content) == 2) {
        distribute_message(idx, seq, clients[idx].topic, content);
        return;
    }

    // Formato: "ACK <seq>" (un subscriber confirma recepcion)
    if (sscanf(buffer, "ACK %u", &seq) == 1) {
        confirm_ack(idx, seq);
        return;
    }

    const char *err = "ERR Formato desconocido. Use MSG <seq> <contenido>\n";
    send_to(idx, err, strlen(err));
}

// ---------- main ----------

int main(void) {
    struct sockaddr_in server_addr, client_addr;
    socklen_t addrlen = sizeof(client_addr);
    char buffer[BUFFER_SIZE];

    memset(clients, 0, sizeof(clients));
    memset(pending, 0, sizeof(pending));

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(PORT);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); exit(EXIT_FAILURE);
    }

    printf("Broker QUIC-like escuchando en el puerto %d...\n", PORT);
    printf("Caracteristicas: ACKs + Retransmision + Numeros de secuencia\n\n");

    while (1) {
        // select con timeout para poder revisar retransmisiones periodicamente
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sockfd, &rfds);
        struct timeval tv = { 0, TIMEOUT_US / 4 }; // revisar cada 125 ms

        int ready = select(sockfd + 1, &rfds, NULL, NULL, &tv);

        // Siempre revisamos retransmisiones
        check_retransmissions();

        if (ready <= 0) continue;

        memset(buffer, 0, BUFFER_SIZE);
        int bytes = recvfrom(sockfd, buffer, BUFFER_SIZE - 1, 0,
                             (struct sockaddr *)&client_addr, &addrlen);
        if (bytes < 0) { perror("recvfrom"); continue; }
        buffer[bytes] = '\0';
        trim_newline(buffer);

        int idx = find_client(&client_addr);
        if (idx == -1) {
            idx = add_client(&client_addr);
            if (idx == -1) {
                sendto(sockfd, "ERR Broker lleno\n", 17, 0,
                       (struct sockaddr *)&client_addr, addrlen);
                continue;
            }
            printf("Nuevo cliente QUIC: %s:%d\n",
                   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            configure_client(idx, buffer);
            continue;
        }

        if (clients[idx].type == CLIENT_UNKNOWN) {
            configure_client(idx, buffer);
        } else {
            handle_message(idx, buffer);
        }
    }

    close(sockfd);
    return 0;
}
