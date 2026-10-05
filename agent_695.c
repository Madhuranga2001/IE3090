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
#define TOKEN     "OPS-2695"
#define REG_NO    "IT24102695"
#define LOG_FILE  "remoteops_" REG_NO ".log"
#define STORE_DIR "./agentfiles/" REG_NO
#define MAX_FILE_SIZE (10LL * 1024 * 1024)     /* 10 MB upload limit */
#define DRAIN_LIMIT   (100LL * 1024 * 1024)    /* too-large uploads up to this are read and thrown away */
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

/* Everything we know about one client (its own copy per thread) */
typedef struct {
    int    fd;
    char   ip[INET_ADDRSTRLEN];
    int    port;
    char   buf[8192];     /* bytes received but not yet processed */
    size_t len;           /* how many bytes are in buf */
    int    authed;        /* 0 until AUTH succeeds */
} client_t;

/* Keep sending until every byte has gone out */
static int send_all(int fd, const char *p, size_t len) {
    while (len > 0) {
        ssize_t s = send(fd, p, len, MSG_NOSIGNAL);
        if (s < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += s;
        len -= (size_t)s;
    }
    return 0;
}

/* Send one response line. The SID tag is added here, and only here. */
static void reply(client_t *c, const char *fmt, ...) {
     char out[8192];
    const int cap = (int)sizeof out - 16;     /* leave room for the tag */
    va_list ap;

    va_start(ap, fmt);
    int n = vsnprintf(out, (size_t)cap, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= cap) n = cap - 1;                /* message was cut short */

    n += snprintf(out + n, sizeof out - (size_t)n, " SID:" SID "\n");
    send_all(c->fd, out, (size_t)n);
}

/* Read one full line (up to the \n) into out.
   Returns 1 = got a line, 0 = client closed, -1 = error or line too long */
static int read_line(client_t *c, char *out, size_t max) {
    for (;;) {
        char *nl = memchr(c->buf, '\n', c->len);
        if (nl) {
            size_t n = (size_t)(nl - c->buf);
            size_t used = n + 1;
            if (n >= max) n = max - 1;
            memcpy(out, c->buf, n);
            out[n] = '\0';
            if (n > 0 && out[n - 1] == '\r') out[n - 1] = '\0';

            memmove(c->buf, c->buf + used, c->len - used);  /* keep the leftovers */
            c->len -= used;
            return 1;
        }
        if (c->len == sizeof c->buf) return -1;     /* no newline and buffer full */

        ssize_t r = recv(c->fd, c->buf + c->len, sizeof c->buf - c->len, 0);
        if (r == 0) return 0;
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        c->len += (size_t)r;
    }
}
/* SYSINFO: "<cpu_load> <mem_used_mb> <uptime_sec>" from /proc */
static int get_sysinfo(char *out, size_t max) {
    double load = 0, up = 0;
    long total_kb = 0, avail_kb = 0;
    FILE *f;

    f = fopen("/proc/loadavg", "r");
    if (!f) return -1;
    if (fscanf(f, "%lf", &load) != 1) { fclose(f); return -1; }
    fclose(f);

    f = fopen("/proc/meminfo", "r");
    if (!f) return -1;
    char key[64];
    long val;
    while (fscanf(f, "%63s %ld", key, &val) == 2) {
        if (strcmp(key, "MemTotal:") == 0) total_kb = val;
        else if (strcmp(key, "MemAvailable:") == 0) avail_kb = val;
        int ch;
        while ((ch = fgetc(f)) != '\n' && ch != EOF) { }   /* skip rest of line */
    }
    fclose(f);

    f = fopen("/proc/uptime", "r");
    if (!f) return -1;
    if (fscanf(f, "%lf", &up) != 1) { fclose(f); return -1; }
    fclose(f);

    snprintf(out, max, "%.2f %ld %ld", load, (total_kb - avail_kb) / 1024, (long)up);
    return 0;
}

/* LISTPROC: "pid:name,pid:name,..." on one line */
static int get_procs(char *out, size_t max) {
    FILE *p = popen("ps -eo pid,comm --no-headers", "r");
    if (!p) return -1;

    char line[256];
    size_t used = 0;
    out[0] = '\0';
    while (fgets(line, sizeof line, p)) {
        int pid;
        char name[128];
        if (sscanf(line, "%d %127[^\n]", &pid, name) != 2) continue;
        for (char *q = name; *q; q++)
            if (*q == ' ') *q = '_';
        int w = snprintf(out + used, max - used, "%s%d:%s", used ? "," : "", pid, name);
        if (w < 0 || (size_t)w >= max - used) break;       /* buffer full: stop */
        used += (size_t)w;
    }
    pclose(p);
    return 0;
}

/* EXEC: run one fixed command and squash the output onto one line */
static int run_cmd(const char *cmd, char *out, size_t max) {
    FILE *p = popen(cmd, "r");
    if (!p) return -1;
    size_t n = fread(out, 1, max - 1, p);
    pclose(p);
    out[n] = '\0';
    for (size_t i = 0; i < n; i++)
        if (out[i] == '\n' || out[i] == '\r') out[i] = ' ';
    while (n > 0 && out[n - 1] == ' ') out[--n] = '\0';
    return 0;
}
/* Filenames may contain only letters, digits, . _ - and must not start with a dot */
static int valid_filename(const char *s) {
    size_t n = strlen(s);
    if (n == 0 || n > 200 || s[0] == '.') return 0;
    for (size_t i = 0; i < n; i++) {
        char ch = s[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-'))
            return 0;
    }
    return 1;
}

/* Read exactly `size` bytes: bytes already in c->buf first, then the socket.
   f == NULL means read them and throw them away. Returns 0 on success, -1 on error. */
static int recv_file(client_t *c, FILE *f, long long size) {
    long long left = size;

    if (c->len > 0 && left > 0) {
        size_t t = c->len;
        if ((long long)t > left) t = (size_t)left;
        if (f && fwrite(c->buf, 1, t, f) != t) return -1;
        memmove(c->buf, c->buf + t, c->len - t);
        c->len -= t;
        left -= (long long)t;
    }

    char tmp[4096];
    while (left > 0) {
        size_t want = left < (long long)sizeof tmp ? (size_t)left : sizeof tmp;
        ssize_t r = recv(c->fd, tmp, want, 0);
        if (r == 0) return -1;                       /* client vanished mid-file */
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (f && fwrite(tmp, 1, (size_t)r, f) != (size_t)r) return -1;
        left -= r;
    }
    return 0;
}

/* PUT <filename> <filesize> followed by <filesize> raw bytes.
   Returns 0 to carry on, -1 to close the connection. */
static int do_put(client_t *c, const char *arg) {
    char fname[256];
    long long size;

    if (sscanf(arg, "%255s %lld", fname, &size) != 2 || size < 0) {
        reply(c, "ERR 007 BAD_ARGUMENTS");
        return -1;                       /* we can't know how many bytes follow */
    }

    if (size > MAX_FILE_SIZE) {
        log_event("PUT REJECTED %s:%d: %s too large (%lld bytes)", c->ip, c->port, fname, size);
        if (size > DRAIN_LIMIT) {
            reply(c, "ERR 004 FILE_TOO_LARGE");
            return -1;
        }
        if (recv_file(c, NULL, size) < 0) return -1;
        reply(c, "ERR 004 FILE_TOO_LARGE");
        return 0;
    }

    if (!valid_filename(fname)) {
        log_event("PUT REJECTED %s:%d: bad filename", c->ip, c->port);
        if (recv_file(c, NULL, size) < 0) return -1;
        reply(c, "ERR 009 BAD_FILENAME");
        return 0;
    }

    char tmp_path[512], final_path[512];
    snprintf(tmp_path, sizeof tmp_path, STORE_DIR "/.part_%d", c->fd);
    snprintf(final_path, sizeof final_path, STORE_DIR "/%s", fname);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        if (recv_file(c, NULL, size) < 0) return -1;
        reply(c, "ERR 008 INTERNAL_ERROR");
        return 0;
    }

    int rc = recv_file(c, f, size);
    if (fclose(f) != 0) rc = -1;
    if (rc < 0) {
        remove(tmp_path);
        log_event("PUT FAILED %s:%d: %s (connection lost or disk error)", c->ip, c->port, fname);
        return -1;
    }

    if (rename(tmp_path, final_path) < 0) {
        remove(tmp_path);
        reply(c, "ERR 008 INTERNAL_ERROR");
        return 0;
    }

    log_event("PUT OK %s:%d: %s (%lld bytes)", c->ip, c->port, fname, size);
    reply(c, "OK FILE_RECEIVED %s", fname);
    return 0;
}

/* GET <filename>: header line, then exactly <filesize> raw bytes */
static int do_get(client_t *c, const char *arg) {
    char fname[256];
    if (sscanf(arg, "%255s", fname) != 1) {
        reply(c, "ERR 007 BAD_ARGUMENTS");
        return 0;
    }
    if (!valid_filename(fname)) {
        reply(c, "ERR 005 FILE_NOT_FOUND");
        return 0;
    }

    char path[512];
    snprintf(path, sizeof path, STORE_DIR "/%s", fname);

    FILE *f = fopen(path, "rb");
    struct stat st;
    if (!f || fstat(fileno(f), &st) < 0 || !S_ISREG(st.st_mode)) {
        if (f) fclose(f);
        log_event("GET FAILED %s:%d: %s not found", c->ip, c->port, fname);
        reply(c, "ERR 005 FILE_NOT_FOUND");
        return 0;
    }

    reply(c, "OK FILE_SEND %s %lld", fname, (long long)st.st_size);

    char tmp[4096];
    long long left = st.st_size;
    while (left > 0) {
        size_t want = left < (long long)sizeof tmp ? (size_t)left : sizeof tmp;
        size_t got = fread(tmp, 1, want, f);
        if (got == 0) break;
        if (send_all(c->fd, tmp, got) < 0) {
            fclose(f);
            return -1;
        }
        left -= (long long)got;
    }
    fclose(f);
    if (left > 0) return -1;             /* file shrank while sending: framing is broken */

    log_event("GET OK %s:%d: %s (%lld bytes)", c->ip, c->port, fname, (long long)st.st_size);
    return 0;
}
/* Handle one command line. Returns 0 to carry on, -1 to close the connection. */
static int handle_line(client_t *c, char *line) {
    char cmd[32] = "", arg[256] = "";
    int n = sscanf(line, "%31s %255[^\n]", cmd, arg);
    if (n < 1) return 0;                          /* empty line: ignore it */

    if (strcmp(cmd, "AUTH") == 0)
        log_event("CMD %s:%d: AUTH (token hidden)", c->ip, c->port);
    else
        log_event("CMD %s:%d: %s", c->ip, c->port, line);

    if (strcmp(cmd, "AUTH") == 0) {
        if (n < 2) {
            reply(c, "ERR 007 BAD_ARGUMENTS");
        } else if (strcmp(arg, TOKEN) == 0) {
            c->authed = 1;
            log_event("AUTH OK %s:%d", c->ip, c->port);
            reply(c, "OK AUTHENTICATED");
        } else {
            log_event("AUTH FAILED %s:%d", c->ip, c->port);
            reply(c, "ERR 001 AUTH_FAILED");
        }
        return 0;
    }

        if (!c->authed) {                             /* everything else needs AUTH first */
        reply(c, "ERR 003 NOT_AUTHENTICATED");
        if (strcmp(cmd, "PUT") == 0) return -1;   /* its raw bytes would be read as commands */
        return 0;
    }

    if (strcmp(cmd, "SYSINFO") == 0) {
        char info[128];
        if (get_sysinfo(info, sizeof info) == 0)
            reply(c, "OK SYSINFO %s", info);
        else
            reply(c, "ERR 008 INTERNAL_ERROR");
        return 0;
    }

    if (strcmp(cmd, "LISTPROC") == 0) {
        char procs[7000];
        if (get_procs(procs, sizeof procs) == 0)
            reply(c, "OK PROCS %s", procs);
        else
            reply(c, "ERR 008 INTERNAL_ERROR");
        return 0;
    }

    if (strcmp(cmd, "EXEC") == 0) {
        const char *shell_cmd = NULL;
        if (n < 2) {
            reply(c, "ERR 007 BAD_ARGUMENTS");
            return 0;
        }
        if      (strcmp(arg, "DATE")     == 0) shell_cmd = "date";
        else if (strcmp(arg, "UPTIME")   == 0) shell_cmd = "uptime";
        else if (strcmp(arg, "DISKFREE") == 0) shell_cmd = "df -h /";
        else if (strcmp(arg, "HOSTNAME") == 0) shell_cmd = "hostname";
        else if (strcmp(arg, "WHOAMI")   == 0) shell_cmd = "whoami";

        if (!shell_cmd) {
            log_event("EXEC REJECTED %s:%d: %s", c->ip, c->port, arg);
            reply(c, "ERR 002 COMMAND_NOT_ALLOWED");
            return 0;
        }
        char result[1024];
        if (run_cmd(shell_cmd, result, sizeof result) == 0)
            reply(c, "OK EXEC_RESULT %s", result);
        else
            reply(c, "ERR 008 INTERNAL_ERROR");
        return 0;
    }
    if (strcmp(cmd, "PUT") == 0) {
        if (n < 2) {
            reply(c, "ERR 007 BAD_ARGUMENTS");
            return -1;
        }
        return do_put(c, arg);
    }

    if (strcmp(cmd, "GET") == 0) {
        if (n < 2) {
            reply(c, "ERR 007 BAD_ARGUMENTS");
            return 0;
        }
        return do_get(c, arg);
    }
    if (strcmp(cmd, "QUIT") == 0) {
        reply(c, "OK BYE");
        return -1;
    }

    reply(c, "ERR 006 UNKNOWN_COMMAND");
    return 0;
}

/* The waiter: runs once per client, in its own thread */
static void *client_thread(void *arg) {
    client_t *c = (client_t *)arg;
    char line[1024];

    log_event("CONNECT %s:%d (fd %d)", c->ip, c->port, c->fd);

    for (;;) {
        int r = read_line(c, line, sizeof line);
        if (r <= 0) break;                        /* client left, or error */
        if (handle_line(c, line) < 0) break;      /* QUIT */
    }

    log_event("DISCONNECT %s:%d (fd %d)", c->ip, c->port, c->fd);
    close(c->fd);
    free(c);
    return NULL;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

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

        client_t *c = calloc(1, sizeof *c);       /* calloc: len = 0, authed = 0 */
        if (!c) {
            close(fd);
            continue;
        }
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
