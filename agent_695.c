#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 9410      /* 7000 + 2410 */
#define SID  "5962"    /* 2695 reversed */

int main(void) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);   /* build the restaurant */
    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;                 /* accept from anywhere */
    addr.sin_port = htons(PORT);                       /* our address (port) */

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind");
        return 1;
    }
    listen(server_fd, 10);                             /* open the doors */
    printf("Agent listening on port %d\n", PORT);

    int client_fd = accept(server_fd, NULL, NULL);     /* wait for a customer */
    printf("Client connected\n");

    char buf[1024];
    ssize_t n = recv(client_fd, buf, sizeof buf - 1, 0);   /* hear their order */
    if (n > 0) {
        buf[n] = '\0';
        printf("Got: %s", buf);
        char reply[] = "OK HELLO SID:" SID "\n";
        send(client_fd, reply, strlen(reply), 0);          /* answer */
    }

    close(client_fd);
    close(server_fd);
    return 0;
}
