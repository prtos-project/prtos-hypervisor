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

/*
 * Deferred newline injection state.
 *
 * When a TCP client connects, getty has likely already printed its
 * initial login prompt (consumed by stdout only).  We inject '\n'
 * into the Guest RX buffer so getty re-displays "login:" for the
 * newly connected client.
 *
 * Injection is deferred because the frontend's PTY bridge may not
 * be actively draining RX yet (or getty's tcsetattr may flush it).
 * We retry multiple times until getty responds (tx_tail advances).
 *
 * Each poll iteration ≈ 1ms (usleep(1000) in main loop).
 */
#define INJECT_INITIAL_DELAY_MS  300    /* Wait before first injection */
#define INJECT_RETRY_INTERVAL_MS 500    /* Interval between retries */
#define INJECT_MAX_RETRIES       5      /* Max injection attempts */

static int inject_counter = -1;        /* Countdown to next injection (ms) */
static int inject_retries = 0;         /* Remaining retry attempts */
static uint32_t inject_tx_baseline = 0; /* tx_tail snapshot for response check */
static int inject_baseline_set = 0;    /* Whether baseline has been captured */

/* Guest halt state — set by virtio_console_notify_guest_halt() */
static volatile int guest_halted = 0;

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
    guest_halted = 0;
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

/*
 * Notify the console backend that the Guest partition has halted.
 * Called from the main loop when guest halt is detected.
 * Disconnects the TCP client so the telnet user gets back to shell.
 */
void virtio_console_notify_guest_halt(void)
{
    guest_halted = 1;
    if (client_fd >= 0) {
        /* Send a notification message to the telnet client */
        static const char msg[] =
            "\r\n\r\n[PRTOS] Guest partition has halted.\r\n"
            "[PRTOS] Connection closed.\r\n";
        (void)write(client_fd, msg, sizeof(msg) - 1);
        close(client_fd);
        client_fd = -1;
        iac_state = 0;
        inject_retries = 0;
        inject_counter = -1;
        printf("[Backend] Console TCP client disconnected (guest halted)\n");
    }
}

static void close_client(void)
{
    if (client_fd >= 0) {
        close(client_fd);
        client_fd = -1;
    }
    iac_state = 0;
    inject_retries = 0;
    inject_counter = -1;
    inject_baseline_set = 0;
}

void virtio_console_process(struct virtio_console_shm *con)
{
    uint32_t head, tail;

    if (!con)
        return;

    /* Don't accept new connections if guest is halted */
    if (guest_halted) {
        /* Still drain TX buffer to stdout (show final shutdown messages) */
        __sync_synchronize();
        head = con->tx_head;
        tail = con->tx_tail;
        while (tail != head) {
            putchar(con->tx_buf[tail % con->buf_size]);
            tail = (tail + 1) % con->buf_size;
        }
        if (con->tx_tail != tail) {
            con->tx_tail = tail;
            __sync_synchronize();
            fflush(stdout);
        }
        return;
    }

    /* Accept new TCP connection (non-blocking) */
    if (listen_fd >= 0 && client_fd < 0) {
        int fd = accept(listen_fd, NULL, NULL);
        if (fd >= 0) {
            int flags = fcntl(fd, F_GETFL, 0);
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);

            /* Disable Nagle for interactive terminal */
            int opt = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

            /* Enable TCP keepalive so dead connections are detected.
             * - Idle time before first probe: 10s
             * - Interval between probes: 5s
             * - Max failed probes before disconnect: 3
             * Total dead-connection detection: ~25s */
            opt = 1;
            setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));
            opt = 10;
            setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &opt, sizeof(opt));
            opt = 5;
            setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &opt, sizeof(opt));
            opt = 3;
            setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &opt, sizeof(opt));

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
            iac_state = 0;
            printf("[Backend] Console TCP client connected\n");

            /* Schedule deferred newline injection.
             * Getty has already printed the initial prompt before the
             * TCP client connected (consumed by stdout only).  A '\n'
             * causes getty to re-display "login:" for the TCP client.
             *
             * We defer injection to allow the frontend PTY bridge to
             * settle after potential getty respawn (tcsetattr may flush). */
            inject_counter = INJECT_INITIAL_DELAY_MS;
            inject_retries = INJECT_MAX_RETRIES;
            inject_baseline_set = 0;
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
                printf("[Backend] Console TCP client write error\n");
                close_client();
            }
        }
        tail = (tail + 1) % con->buf_size;
    }

    if (con->tx_tail != tail) {
        con->tx_tail = tail;
        __sync_synchronize();
        fflush(stdout);
    }

    /* Deferred newline injection with retry.
     * Placed AFTER the TX read section so that the baseline tx_tail
     * reflects the state after draining any stale data. */
    if (inject_retries > 0 && client_fd >= 0) {
        if (!inject_baseline_set) {
            /* Capture tx_tail baseline AFTER stale TX data is drained */
            inject_tx_baseline = con->tx_tail;
            inject_baseline_set = 1;
        }
        /* Check if getty already responded (tx_tail advanced) */
        __sync_synchronize();
        if (con->tx_tail != inject_tx_baseline) {
            /* Getty produced output — login prompt sent, stop injecting */
            inject_retries = 0;
            inject_counter = -1;
        } else if (inject_counter > 0) {
            inject_counter--;
        } else {
            /* Time to inject '\n' */
            uint32_t rx_h = con->rx_head;
            uint32_t rx_next = (rx_h + 1) % con->buf_size;
            if (rx_next != con->rx_tail) {
                con->rx_buf[rx_h] = '\n';
                con->rx_head = rx_next;
                __sync_synchronize();
            }
            inject_retries--;
            if (inject_retries > 0)
                inject_counter = INJECT_RETRY_INTERVAL_MS;
            else
                inject_counter = -1;
        }
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
                    if (ch == TELNET_IAC)
                        iac_state = 4;
                    continue;
                }
                if (iac_state == 4) {
                    if (ch == 240) {
                        iac_state = 0;  /* SE: subneg complete */
                    } else if (ch == TELNET_IAC) {
                        iac_state = 4;  /* escaped 0xFF inside SB */
                    } else {
                        iac_state = 3;  /* back to scanning */
                    }
                    continue;
                }
                if (iac_state == 1) {
                    if (ch == TELNET_IAC) {
                        /* IAC IAC = escaped literal 0xFF */
                        iac_state = 0;
                        uint32_t next_head = (head + 1) % con->buf_size;
                        if (next_head == con->rx_tail)
                            break;
                        con->rx_buf[head] = (char)0xFF;
                        head = next_head;
                    } else if (ch >= TELNET_WILL && ch <= TELNET_DONT) {
                        iac_state = 2;
                    } else if (ch == 250) {
                        iac_state = 3;  /* SB subnegotiation */
                    } else {
                        iac_state = 0;  /* 2-byte command done */
                    }
                    continue;
                }
                if (iac_state == 2) {
                    iac_state = 0;      /* option byte consumed */
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
            close_client();
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            printf("[Backend] Console TCP client error: %s\n", strerror(errno));
            close_client();
        }
    }
}

void virtio_console_cleanup(void)
{
    close_client();
    if (listen_fd >= 0) {
        close(listen_fd);
        listen_fd = -1;
    }
}
