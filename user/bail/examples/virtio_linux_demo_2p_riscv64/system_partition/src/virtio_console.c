/*
 * virtio_console.c - Virtio Console backend for PRTOS System Partition.
 *
 * Uses the Virtio_Con shared memory region at GPA 0x16500000 (256KB).
 *
 * Data flow (bidirectional):
 *   Guest writes to /dev/hvc0 -> tx_buf in shared memory -> Backend reads
 *     -> prints to System stdout AND sends to TCP telnet client
 *   TCP telnet client sends data -> Backend writes to rx_buf in shared memory
 *     -> Frontend reads rx_buf -> writes to PTY master -> Guest reads /dev/hvc0
 *
 * The backend listens on TCP port 4321 for telnet connections to the Guest.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "virtio_be.h"

#define CONSOLE_TCP_PORT    4321
#define CONSOLE_TCP_BACKLOG 1

/* Telnet protocol constants */
#define TELNET_IAC   255
#define TELNET_WILL  251
#define TELNET_WONT  252
#define TELNET_DO    253
#define TELNET_DONT  254
#define TELNET_OPT_ECHO     1
#define TELNET_OPT_SGA      3   /* Suppress Go Ahead */

static int listen_fd = -1;
static int client_fd = -1;

/*
 * Telnet IAC state machine - persists across read() calls to handle
 * IAC sequences split across TCP segment boundaries.
 *
 * States:
 *   0 = normal data
 *   1 = received IAC (0xFF), waiting for command byte
 *   2 = received IAC + WILL/WONT/DO/DONT, waiting for option byte
 *   3 = inside IAC SB subnegotiation, scanning for IAC
 *   4 = inside IAC SB subnegotiation, received IAC, waiting for SE
 */
static int iac_state = 0;

void virtio_console_init(struct virtio_console_shm *con)
{
    struct sockaddr_in addr;
    int opt = 1;
    int flags;

    if (!con)
        return;

    memset(con, 0, sizeof(*con));
    con->magic = VIRTIO_CONSOLE_MAGIC;
    con->version = 1;
    con->buf_size = VIRTIO_CONSOLE_BUF_SIZE;
    con->device_status = VIRTIO_STATUS_ACK;
    con->backend_ready = 1;
    __sync_synchronize();

    /* Create TCP listening socket for Guest console access */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("[Backend] Console TCP socket");
        printf("[Backend] Virtio-Console initialized (buf_size=%u, no TCP)\n",
               con->buf_size);
        return;
    }

    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(CONSOLE_TCP_PORT);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[Backend] Console TCP bind");
        close(listen_fd);
        listen_fd = -1;
        printf("[Backend] Virtio-Console initialized (buf_size=%u, TCP bind failed)\n",
               con->buf_size);
        return;
    }

    if (listen(listen_fd, CONSOLE_TCP_BACKLOG) < 0) {
        perror("[Backend] Console TCP listen");
        close(listen_fd);
        listen_fd = -1;
        printf("[Backend] Virtio-Console initialized (buf_size=%u, TCP listen failed)\n",
               con->buf_size);
        return;
    }

    /* Set non-blocking */
    flags = fcntl(listen_fd, F_GETFL, 0);
    fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK);

    printf("[Backend] Virtio-Console initialized (buf_size=%u, TCP port %d)\n",
           con->buf_size, CONSOLE_TCP_PORT);
}

void virtio_console_process(struct virtio_console_shm *con)
{
    uint32_t head, tail;

    if (!con)
        return;

    /* Accept new TCP connection (non-blocking) */
    if (listen_fd >= 0 && client_fd < 0) {
        int fd = accept(listen_fd, NULL, NULL);
        if (fd >= 0) {
            int flags = fcntl(fd, F_GETFL, 0);
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);

            /* Disable Nagle for interactive terminal */
            int opt = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

            /* Send telnet negotiation: character-at-a-time mode.
             * WILL ECHO: server handles echo (prevents double-echo)
             * WILL SGA:  suppress go-ahead (full-duplex)
             * DO SGA:    request client suppress go-ahead too
             */
            static const unsigned char telnet_init[] = {
                TELNET_IAC, TELNET_WILL, TELNET_OPT_ECHO,
                TELNET_IAC, TELNET_WILL, TELNET_OPT_SGA,
                TELNET_IAC, TELNET_DO,   TELNET_OPT_SGA,
            };
            (void)write(fd, telnet_init, sizeof(telnet_init));

            client_fd = fd;
            iac_state = 0;  /* Reset IAC parser for new client */
            printf("[Backend] Console TCP client connected\n");

            /* Inject newline to make getty re-display login prompt.
             * Getty has already printed the initial prompt before the
             * TCP client connected, so it was consumed (sent to stdout
             * only).  A newline causes getty to see an empty username
             * and re-display "login:", which goes to the TCP client. */
            __sync_synchronize();
            {
                uint32_t rx_h = con->rx_head;
                uint32_t rx_next = (rx_h + 1) % con->buf_size;
                if (rx_next != con->rx_tail) {
                    con->rx_buf[rx_h] = '\n';
                    con->rx_head = rx_next;
                    __sync_synchronize();
                }
            }
        }
    }

    /* Read from Guest's TX buffer and send to stdout + TCP client */
    __sync_synchronize();
    head = con->tx_head;
    tail = con->tx_tail;

    while (tail != head) {
        char c = con->tx_buf[tail % con->buf_size];
        putchar(c);
        if (client_fd >= 0) {
            ssize_t w = write(client_fd, &c, 1);
            if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                close(client_fd);
                client_fd = -1;
            }
        }
        tail = (tail + 1) % con->buf_size;
    }

    if (con->tx_tail != tail) {
        con->tx_tail = tail;
        __sync_synchronize();
        fflush(stdout);
    }

    /* Read from TCP client and write to Guest's RX buffer */
    if (client_fd >= 0) {
        char buf[256];
        ssize_t n = read(client_fd, buf, sizeof(buf));
        if (n > 0) {
            __sync_synchronize();
            head = con->rx_head;
            for (ssize_t i = 0; i < n; i++) {
                unsigned char ch = (unsigned char)buf[i];

                /* Telnet IAC state machine — handles sequences split
                 * across read() boundaries.  States:
                 *   0: normal data
                 *   1: after IAC, waiting for command byte
                 *   2: after IAC WILL/WONT/DO/DONT, waiting for option
                 *   3: inside SB subnegotiation, scanning for IAC
                 *   4: inside SB subneg, got IAC, waiting for SE
                 *
                 * Without this, partial IAC sequences or SB subneg
                 * data (e.g. NAWS window-size bytes) leak into the
                 * guest PTY, potentially killing getty via SIGINT. */
                if (iac_state == 3) {
                    /* Inside SB subnegotiation: discard data until IAC */
                    if (ch == TELNET_IAC)
                        iac_state = 4;
                    continue;
                }
                if (iac_state == 4) {
                    /* SB subneg saw IAC: expect SE(240) to end */
                    if (ch == 240) {
                        iac_state = 0;  /* SE: subneg complete */
                    } else if (ch == TELNET_IAC) {
                        iac_state = 4;  /* IAC IAC inside SB: escaped 0xFF, stay */
                    } else {
                        iac_state = 3;  /* Not SE: back to scanning */
                    }
                    continue;
                }
                if (iac_state == 1) {
                    /* After IAC: expect command byte */
                    if (ch == TELNET_IAC) {
                        /* IAC IAC = escaped literal 0xFF */
                        iac_state = 0;
                        uint32_t next_head = (head + 1) % con->buf_size;
                        if (next_head == con->rx_tail)
                            break;
                        con->rx_buf[head] = (char)0xFF;
                        head = next_head;
                    } else if (ch >= TELNET_WILL && ch <= TELNET_DONT) {
                        /* WILL(251)/WONT(252)/DO(253)/DONT(254):
                         * 3-byte sequence, need option byte next */
                        iac_state = 2;
                    } else if (ch == 250) {
                        /* SB(250): subnegotiation start, discard until IAC SE */
                        iac_state = 3;
                    } else {
                        /* Other IAC commands (NOP, BRK, IP, etc.):
                         * 2-byte sequence, done */
                        iac_state = 0;
                    }
                    continue;
                }
                if (iac_state == 2) {
                    /* After IAC + cmd: this is the option byte, skip it */
                    iac_state = 0;
                    continue;
                }
                if (ch == TELNET_IAC) {
                    iac_state = 1;
                    continue;
                }

                /* Normal data byte */
                uint32_t next_head = (head + 1) % con->buf_size;
                if (next_head == con->rx_tail)
                    break;  /* Ring full */
                con->rx_buf[head] = buf[i];
                head = next_head;
            }
            con->rx_head = head;
            __sync_synchronize();
        } else if (n == 0) {
            printf("[Backend] Console TCP client disconnected\n");
            close(client_fd);
            client_fd = -1;
            iac_state = 0;  /* Reset IAC state for next client */
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            close(client_fd);
            client_fd = -1;
            iac_state = 0;
        }
    }
}

void virtio_console_cleanup(void)
{
    if (client_fd >= 0) {
        close(client_fd);
        client_fd = -1;
    }
    if (listen_fd >= 0) {
        close(listen_fd);
        listen_fd = -1;
    }
}
