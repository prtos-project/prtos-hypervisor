/*
 * set_serial_poll - Set a serial port to polling mode (irq=0).
 *
 * When IRQ-based serial I/O doesn't work (e.g., under a hypervisor that
 * doesn't route legacy IRQs), setting irq=0 forces the 8250 driver to
 * use timer-based polling instead.
 *
 * Usage: set_serial_poll /dev/ttyS1
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/serial.h>

int main(int argc, char *argv[])
{
    struct serial_struct ss;
    const char *dev;
    int fd;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s /dev/ttySN\n", argv[0]);
        return 1;
    }
    dev = argv[1];

    fd = open(dev, O_RDWR | O_NONBLOCK | O_NOCTTY);
    if (fd < 0) {
        perror(dev);
        return 1;
    }

    if (ioctl(fd, TIOCGSERIAL, &ss) < 0) {
        perror("TIOCGSERIAL");
        close(fd);
        return 1;
    }

    if (ss.irq == 0) {
        close(fd);
        return 0;  /* Already in polling mode */
    }

    ss.irq = 0;
    if (ioctl(fd, TIOCSSERIAL, &ss) < 0) {
        perror("TIOCSSERIAL");
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}
