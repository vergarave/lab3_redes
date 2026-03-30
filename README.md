# Laboratorio 3 - Análisis Capa de Transporte y Sockets

**Curso:** Infraestructura de Comunicaciones  
**Universidad:** Universidad de los Andes

**Integrantes:**
- Daniel Vergara - 202320392
- Jose Manuel Fonseca - 202122456
- Mariana Cediel - 202321548

---

## Descripción general

Sistema de noticias deportivas en tiempo real implementado con el modelo
publicación–suscripción en tres versiones:

| Versión | Archivos | Puerto |
|---------|----------|--------|
| TCP     | `broker_tcp.c`, `publisher_tcp.c`, `subscriber_tcp.c` | 5000 |
| UDP     | `broker_udp.c`, `publisher_udp.c`, `subscriber_udp.c` | 5001 |
| QUIC-like (Bono) | `broker_quic.c`, `publisher_quic.c`, `subscriber_quic.c` | 5002 |

---

## Compilación

```bash
# TCP
gcc broker_tcp.c     -o broker_tcp
gcc publisher_tcp.c  -o publisher_tcp
gcc subscriber_tcp.c -o subscriber_tcp

# UDP
gcc broker_udp.c     -o broker_udp
gcc publisher_udp.c  -o publisher_udp
gcc subscriber_udp.c -o subscriber_udp

# QUIC-like (Bono 1)
gcc broker_quic.c     -o broker_quic
gcc publisher_quic.c  -o publisher_quic
gcc subscriber_quic.c -o subscriber_quic
```

---

## Ejecución

Abrir terminales separadas para cada proceso. Ejecutar siempre el broker primero.

### TCP
```bash
# Terminal 1
./broker_tcp

# Terminal 2
./subscriber_tcp
# Ingresar tema: Colombia vs Brasil

# Terminal 3
./subscriber_tcp
# Ingresar tema: Colombia vs Brasil

# Terminal 4
./publisher_tcp
# Ingresar tema: Colombia vs Brasil
# Escribir mensajes

# Terminal 5
./publisher_tcp
# Ingresar tema: Argentina vs Uruguay
```

### UDP (mismo flujo, distinto puerto)
```bash
./broker_udp
./subscriber_udp
./publisher_udp
```

### QUIC-like (Bono)
```bash
./broker_quic
./subscriber_quic
./publisher_quic
# Cada mensaje enviado espera ACK del broker.
# Si no llega ACK en 1 segundo, retransmite automáticamente (hasta 5 veces).
# Los subscribers envían ACK al broker por cada mensaje recibido.
# Los mensajes incluyen número de secuencia para detectar desorden.
```

---

## Librerías utilizadas y documentación de funciones clave

Este proyecto usa únicamente las cabeceras estándar de POSIX para sockets.
No se usan librerías externas de alto nivel.

### Cabeceras incluidas

| Cabecera | Propósito |
|----------|-----------|
| `<sys/socket.h>` | `socket()`, `bind()`, `listen()`, `accept()`, `connect()`, `send()`, `recv()`, `sendto()`, `recvfrom()` |
| `<arpa/inet.h>` | `htons()`, `inet_pton()`, `inet_ntoa()` |
| `<netinet/in.h>` | Estructuras `sockaddr_in`, constantes `INADDR_ANY` |
| `<sys/select.h>` | `select()` para multiplexación de I/O |
| `<unistd.h>` | `close()` |

### Funciones clave — TCP

| Función | Uso en este proyecto |
|---------|----------------------|
| `socket(AF_INET, SOCK_STREAM, 0)` | Crea socket TCP orientado a conexión |
| `setsockopt(..., SO_REUSEADDR, ...)` | Permite reutilizar el puerto al reiniciar el broker |
| `bind(fd, addr, len)` | Vincula el socket del broker a IP:puerto |
| `listen(fd, backlog)` | Pone el socket en modo escucha |
| `accept(fd, &addr, &len)` | Acepta una conexión entrante; devuelve nuevo fd |
| `connect(fd, &addr, len)` | El cliente establece conexión con el broker |
| `send(fd, buf, len, 0)` | Envía datos por la conexión TCP |
| `recv(fd, buf, len, 0)` | Recibe datos de la conexión TCP |
| `select(maxfd+1, &rfds, ...)` | Multiplexación: monitorea múltiples sockets sin hilos |
| `close(fd)` | Cierra el socket y libera recursos |

### Funciones clave — UDP

| Función | Uso en este proyecto |
|---------|----------------------|
| `socket(AF_INET, SOCK_DGRAM, 0)` | Crea socket UDP sin conexión |
| `bind(fd, addr, len)` | Vincula el socket del broker (no se usa en clientes) |
| `sendto(fd, buf, len, 0, &addr, addrlen)` | Envía un datagrama especificando destino en cada llamada |
| `recvfrom(fd, buf, len, 0, &addr, &addrlen)` | Recibe un datagrama y obtiene la dirección del remitente |

### Funciones clave — QUIC-like (Bono)

Reutiliza las mismas funciones UDP más:

| Función | Uso en este proyecto |
|---------|----------------------|
| `select(fd+1, &rfds, NULL, NULL, &tv)` | Espera ACK con timeout configurable |
| `clock_gettime(CLOCK_MONOTONIC, &ts)` | Mide el tiempo transcurrido para detectar mensajes sin ACK |

---

## Protocolo QUIC-like (Bono 1)

El sistema QUIC implementa sobre UDP:

1. **Números de secuencia:** cada mensaje lleva un campo `seq` que se incrementa.  
   Formato: `MSG <seq> <contenido>`

2. **ACKs:** el broker confirma cada mensaje al publisher con `ACK <seq>`.  
   Los subscribers confirman al broker con `ACK <seq>` tras recibir cada mensaje.

3. **Retransmisión:** si el publisher no recibe `ACK` en 1 segundo, reenvía el
   mismo mensaje (hasta 5 veces). El broker hace lo mismo con los subscribers.

4. **Detección de orden:** el subscriber compara el `seq` recibido con el
   `expected_seq` y reporta si un mensaje llegó fuera de orden.

