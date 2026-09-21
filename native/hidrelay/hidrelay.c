#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <hidapi/hidapi.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;

static void stop_handler(int signal_number)
{
    (void)signal_number;
    stopping = 1;
}

static int read_full(int fd, uint8_t *buffer, size_t size)
{
    size_t offset = 0;
    while (offset < size) {
        ssize_t count = recv(fd, buffer + offset, size - offset, 0);
        if (count == 0) return 0;
        if (count < 0 && errno == EINTR) {
            if (stopping) return 0;
            continue;
        }
        if (count < 0) return -1;
        offset += (size_t)count;
    }
    return 1;
}

static int parse_port(const char *text, int *port)
{
    unsigned long value = 0;
    if (text == NULL || *text == '\0') return 0;
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') return 0;
        if (value > (65535u - (*cursor - '0')) / 10u) return 0;
        value = value * 10u + (*cursor - '0');
    }
    if (value == 0) return 0;
    *port = (int)value;
    return 1;
}

static hid_device *open_dualsense(unsigned *enumerated, unsigned *open_failures)
{
    struct hid_device_info *devices = hid_enumerate(0x054c, 0x0ce6);
    hid_device *handle = NULL;
    *enumerated = 0;
    *open_failures = 0;
    for (struct hid_device_info *device = devices; device; device = device->next) {
        ++*enumerated;
        handle = hid_open_path(device->path);
        if (handle) break;
        ++*open_failures;
    }
    hid_free_enumeration(devices);
    return handle;
}

static void close_socket(int *fd)
{
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

static int install_signal_handlers(void)
{
    struct sigaction action = {0};
    action.sa_handler = stop_handler;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) != 0 || sigaction(SIGTERM, &action, NULL) != 0) return 0;
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) return 0;
    return 1;
}

int main(void)
{
    if (!install_signal_handlers()) return 1;
    int port = 28766;
    const char *port_text = getenv("ONIMUSHA_HIDRELAY_PORT");
    if (port_text && !parse_port(port_text, &port)) {
        fprintf(stderr, "Onimusha HID relay invalid ONIMUSHA_HIDRELAY_PORT=%s\n", port_text);
        return 2;
    }
    if (hid_init() != 0) {
        fprintf(stderr, "Onimusha HID relay hidapi initialization failed\n");
        return 3;
    }

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        fprintf(stderr, "Onimusha HID relay socket failed: %s\n", strerror(errno));
        hid_exit();
        return 4;
    }
    int reuse = 1;
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse) != 0) {
        fprintf(stderr, "Onimusha HID relay socket setup failed: %s\n", strerror(errno));
        close_socket(&listener);
        hid_exit();
        return 4;
    }
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons((uint16_t)port),
        .sin_addr = { .s_addr = htonl(INADDR_LOOPBACK) },
    };
    if (bind(listener, (struct sockaddr *)&address, sizeof address) < 0 || listen(listener, 1) < 0) {
        fprintf(stderr, "Onimusha HID relay bind/listen failed on 127.0.0.1:%d: %s\n",
            port, strerror(errno));
        close_socket(&listener);
        hid_exit();
        return 5;
    }
    fprintf(stderr, "Onimusha HID relay listening on 127.0.0.1:%d\n", port);
    while (!stopping) {
        int client = accept(listener, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR && !stopping) continue;
            if (!stopping) fprintf(stderr, "Onimusha HID relay accept failed: %s\n", strerror(errno));
            break;
        }
        unsigned enumerated = 0, open_failures = 0;
        hid_device *controller = open_dualsense(&enumerated, &open_failures);
        if (!controller) {
            fprintf(stderr, "Onimusha HID relay DualSense open failed (enumerated=%u open_failures=%u)\n",
                enumerated, open_failures);
            close_socket(&client);
            continue;
        }
        uint8_t report[48];
        while (!stopping) {
            int result = read_full(client, report, sizeof report);
            if (result <= 0) {
                if (result < 0) fprintf(stderr, "Onimusha HID relay client read failed: %s\n", strerror(errno));
                break;
            }
            if (hid_write(controller, report, sizeof report) != (int)sizeof report) {
                fprintf(stderr, "Onimusha HID relay controller write failed\n");
                break;
            }
        }
        hid_close(controller);
        close_socket(&client);
    }
    close_socket(&listener);
    hid_exit();
    return 0;
}
