#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT      9410                     /* 7000 + 2410 */
#define SID       "5962"                   /* 2695 reversed */
#define REG_NO    "IT24102695"
#define LOG_FILE  "remoteops_" REG_NO ".log"
#define STORE_DIR "./agentfiles/" REG_NO

/* One lock so two threads never write to the log at the same moment */
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

/* Write one timestamped line to the log file and to the screen */
static void log_event(const char *fmt, ...) {
    char ts[32];
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tmv);

    pthread_mutex_lock(&log_lock);

    FILE *f = fopen(LOG_FILE, "a");
    va_list ap;
    if (f) {
        fprintf(f, "[%s] ", ts);
        va_start(ap, fmt);
        vfprintf(f, fmt, ap);
        va_end(ap);
        fputc('\n', f);
        fclose(f);
    }

    printf("[%s] ", ts);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');

    pthread_mutex_unlock(&log_lock);
}

/* Everything we need to know about one client */
typedef struct {
    int  fd;
    char ip[INET_ADDRSTRLEN];
    int  port;
} client_t;

/* The waiter: runs once per client, in its own thread */
static void *client_thread(void *arg) {
    client_t *c = (client_t *)arg;
    char buf[1024];
    ssize_t n;

    log_event("CONNECT %s:%d (fd %d)", c->ip, c->port, c->fd);

    while ((n = recv(c->fd, buf, sizeof buf - 1, 0)) > 0) {
        buf[n] = '\0';
        buf[strcspn(buf, "\r\n")] = '\0';
        log_event("RECV %s:%d: %s", c->ip, c->port, buf);

        char reply[] = "OK HELLO SID:" SID "\n";
        send(c->fd, reply, strlen(reply), MSG_NOSIGNAL);
    }

    log_event("DISCONNECT %s:%d (fd %d)", c->ip, c->port, c->fd);
    close(c->fd);
    free(c);
    return NULL;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);   /* a dead client must not kill the Agent */

    if (mkdir("./agentfiles", 0755) < 0 && errno != EEXIST) {
        perror("mkdir agentfiles");
        return 1;
    }
    if (mkdir(STORE_DIR, 0755) < 0 && errno != EEXIST) {
        perror("mkdir storage dir");
        return 1;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind");
        return 1;
    }
    listen(server_fd, 10);
    log_event("Agent started, listening on port %d, storage %s", PORT, STORE_DIR);

    for (;;) {
        struct sockaddr_in cli;
        socklen_t len = sizeof cli;
        int fd = accept(server_fd, (struct sockaddr *)&cli, &len);
        if (fd < 0) {
            perror("accept");
            continue;
        }

        client_t *c = malloc(sizeof *c);
        c->fd = fd;
        inet_ntop(AF_INET, &cli.sin_addr, c->ip, sizeof c->ip);
        c->port = ntohs(cli.sin_port);

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread, c) != 0) {
            perror("pthread_create");
            close(fd);
            free(c);
            continue;
        }
        pthread_detach(tid);
    }
}
