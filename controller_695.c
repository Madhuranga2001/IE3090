#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 9410            /* 7000 + 2410 */

static int    sock = -1;             /* TCP connection to the Agent */
static char   rbuf[16384];           /* bytes received but not used yet */
static size_t rlen = 0;

/* UDP monitoring listener */
static int          udp_fd = -1;
static pthread_t    udp_tid;
static volatile int udp_active = 0;

/* Keep sending until every byte has gone out */
static int send_all(const char *p, size_t len) {
    while (len > 0) {
        ssize_t s = send(sock, p, len, MSG_NOSIGNAL);
        if (s < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += s;
        len -= (size_t)s;
    }
    return 0;
}

/* Read one full reply line from the Agent.
   Returns 1 = got a line, 0 = Agent closed the connection, -1 = error */
static int read_line(char *out, size_t max) {
    for (;;) {
        char *nl = memchr(rbuf, '\n', rlen);
        if (nl) {
            size_t n = (size_t)(nl - rbuf);
            size_t used = n + 1;
            if (n >= max) n = max - 1;
            memcpy(out, rbuf, n);
            out[n] = '\0';
            if (n > 0 && out[n - 1] == '\r') out[n - 1] = '\0';
            memmove(rbuf, rbuf + used, rlen - used);   /* keep the leftovers */
            rlen -= used;
            return 1;
        }
        if (rlen == sizeof rbuf) return -1;

        ssize_t r = recv(sock, rbuf + rlen, sizeof rbuf - rlen, 0);
        if (r == 0) return 0;
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        rlen += (size_t)r;
    }
}

/* Thread that prints every UDP datagram the Agent sends */
static void *udp_thread(void *arg) {
    (void)arg;
    char buf[512];
    while (udp_active) {
        ssize_t n = recvfrom(udp_fd, buf, sizeof buf - 1, 0, NULL, NULL);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            break;
        }
        buf[n] = '\0';
        buf[strcspn(buf, "\r\n")] = '\0';
        printf("\n[UDP] %s\n> ", buf);
        fflush(stdout);
    }
    return NULL;
}

static int start_udp(int port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;

    struct timeval tv = { 1, 0 };            /* wake up every second to check udp_active */
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((unsigned short)port);
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) {
        perror("udp bind");
        close(fd);
        return -1;
    }

    udp_fd = fd;
    udp_active = 1;
    if (pthread_create(&udp_tid, NULL, udp_thread, NULL) != 0) {
        udp_active = 0;
        close(fd);
        udp_fd = -1;
        return -1;
    }
    return 0;
}

static void stop_udp(void) {
    if (!udp_active) return;
    udp_active = 0;
    pthread_join(udp_tid, NULL);
    close(udp_fd);
    udp_fd = -1;
}

static void print_rate(const char *what, long long bytes,
                       struct timespec a, struct timespec b) {
    double secs = (double)(b.tv_sec - a.tv_sec) + (double)(b.tv_nsec - a.tv_nsec) / 1e9;
    if (secs < 1e-6) secs = 1e-6;
    printf("[%s] %lld bytes in %.3f s = %.0f bytes/s (%.2f MB/s)\n",
           what, bytes, secs, (double)bytes / secs, (double)bytes / secs / 1048576.0);
}
/* PUT <local file>: send the header, then exactly <size> raw bytes.
   Returns 1 to carry on, 0 to quit. */
static int do_put(const char *path) {
    if (path[0] == '\0') {
        printf("Usage: PUT <local file>\n");
        return 1;
    }
    struct stat st;
    if (stat(path, &st) < 0 || !S_ISREG(st.st_mode)) {
        printf("Cannot read file '%s'\n", path);
        return 1;
    }
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    if (*name == '\0' || strchr(name, ' ')) {
        printf("File name must not be empty or contain spaces\n");
        return 1;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror("fopen");
        return 1;
    }

    char hdr[400];
    int hn = snprintf(hdr, sizeof hdr, "PUT %s %lld\n", name, (long long)st.st_size);
    if (hn < 0 || hn >= (int)sizeof hdr) {
        fclose(f);
        printf("File name too long\n");
        return 1;
    }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    if (send_all(hdr, (size_t)hn) < 0) {
        fclose(f);
        printf("Connection lost\n");
        return 0;
    }

    char buf[4096];
    size_t got;
    long long sent = 0;
    while ((got = fread(buf, 1, sizeof buf, f)) > 0) {
        if (send_all(buf, got) < 0) {
            fclose(f);
            printf("Connection lost while sending\n");
            return 0;
        }
        sent += (long long)got;
    }
    fclose(f);

    char resp[16384];
    int r = read_line(resp, sizeof resp);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    if (r <= 0) {
        printf("Connection closed by Agent\n");
        return 0;
    }
    printf("%s\n", resp);
    if (strncmp(resp, "OK ", 3) == 0)
        print_rate("PUT", sent, t0, t1);
    return 1;
}

/* GET <name>: read the header, then exactly <size> raw bytes into downloads/<name> */
static int do_get(const char *name) {
    if (name[0] == '\0') {
        printf("Usage: GET <file name on the Agent>\n");
        return 1;
    }

    char out[600];
    int n = snprintf(out, sizeof out, "GET %s\n", name);
    if (n < 0 || n >= (int)sizeof out) {
        printf("File name too long\n");
        return 1;
    }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    if (send_all(out, (size_t)n) < 0) {
        printf("Connection lost\n");
        return 0;
    }

    char resp[16384];
    int r = read_line(resp, sizeof resp);
    if (r <= 0) {
        printf("Connection closed by Agent\n");
        return 0;
    }

    char fname[256];
    long long size;
    if (sscanf(resp, "OK FILE_SEND %255s %lld", fname, &size) != 2 || size < 0) {
        printf("%s\n", resp);                 /* an ERR line */
        return 1;
    }
    if (strchr(fname, '/') || strcmp(fname, "..") == 0) {
        printf("Refusing unsafe file name from the Agent\n");
        return 0;
    }

    mkdir("downloads", 0755);
    char path[600];
    snprintf(path, sizeof path, "downloads/%s", fname);
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror("fopen");
        return 0;                              /* the raw bytes are coming; close the session */
    }

    long long left = size;
    if (rlen > 0 && left > 0) {                /* bytes that arrived with the header line */
        size_t t = rlen;
        if ((long long)t > left) t = (size_t)left;
        if (fwrite(rbuf, 1, t, f) != t) {
            fclose(f);
            return 0;
        }
        memmove(rbuf, rbuf + t, rlen - t);
        rlen -= t;
        left -= (long long)t;
    }

    char buf[4096];
    while (left > 0) {
        size_t want = left < (long long)sizeof buf ? (size_t)left : sizeof buf;
        ssize_t k = recv(sock, buf, want, 0);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) break;
        if (fwrite(buf, 1, (size_t)k, f) != (size_t)k) break;
        left -= k;
    }
    fclose(f);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    if (left > 0) {
        printf("Download incomplete\n");
        remove(path);
        return 0;
    }
    printf("%s\n", resp);
    printf("Saved to %s\n", path);
    print_rate("GET", size, t0, t1);
    return 1;
}

/* Every other command: send the line as typed, print the reply */
static int do_simple(const char *line, const char *cmd, const char *a1, const char *a2) {
    int mon_start = (strcmp(cmd, "MONITOR") == 0 && strcmp(a1, "START") == 0);
    int mon_stop  = (strcmp(cmd, "MONITOR") == 0 && strcmp(a1, "STOP") == 0);
    int quit      = (strcmp(cmd, "QUIT") == 0);
    int started   = 0;

    if (mon_start && !udp_active) {
        int p = atoi(a2);
        if (p >= 1024 && p <= 65535) {         /* listen BEFORE asking the Agent to send */
            if (start_udp(p) < 0) {
                printf("Could not listen on UDP port %d\n", p);
                return 1;
            }
            started = 1;
        }                                      /* otherwise just send it: the Agent will reject it */
    }

    char out[1100];
    int n = snprintf(out, sizeof out, "%s\n", line);
    if (n < 0 || n >= (int)sizeof out || send_all(out, (size_t)n) < 0) {
        printf("Connection lost\n");
        return 0;
    }

    char resp[16384];
    int r = read_line(resp, sizeof resp);
    if (r <= 0) {
        printf("Connection closed by Agent\n");
        return 0;
    }
    printf("%s\n", resp);

    int ok = (strncmp(resp, "OK", 2) == 0);
    if (started && !ok) stop_udp();            /* the Agent said no: close our listener */
    if (mon_stop && ok) stop_udp();
    if (quit && ok) {
        stop_udp();
        return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    const char *host = argc > 1 ? argv[1] : DEFAULT_HOST;
    int port = argc > 2 ? atoi(argv[2]) : DEFAULT_PORT;

    signal(SIGPIPE, SIG_IGN);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        fprintf(stderr, "Bad IP address: %s\n", host);
        return 1;
    }
    if (connect(sock, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("connect");
        return 1;
    }

    printf("Connected to %s:%d\n", host, port);
    printf("Commands: AUTH <token>, SYSINFO, LISTPROC, EXEC <name>, PUT <file>, GET <name>,\n");
    printf("          MONITOR START <udp port>, MONITOR STOP, QUIT\n> ");
    fflush(stdout);

    char line[1024];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = '\0';

        char cmd[32] = "", a1[256] = "", a2[256] = "";
        if (sscanf(line, "%31s %255s %255s", cmd, a1, a2) < 1) {
            printf("> ");
            fflush(stdout);
            continue;
        }

        int keep;
        if (strcmp(cmd, "PUT") == 0)      keep = do_put(a1);
        else if (strcmp(cmd, "GET") == 0) keep = do_get(a1);
        else                              keep = do_simple(line, cmd, a1, a2);
        if (!keep) break;

        printf("> ");
        fflush(stdout);
    }

    stop_udp();
    close(sock);
    printf("Disconnected\n");
    return 0;
}
