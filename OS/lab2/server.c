/*
 * TCP-сервер на pselect().
 *
 * - Сообщает о каждом новом подключении. Первое соединение остаётся
 *   открытым, остальные закрываются сразу после принятия.
 * - При появлении данных в сохранённом соединении выводит их количество.
 */

#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 12345

int main(void)
{
    int server_fd;
    int client_fd = -1;
    int opt = 1;
    struct sockaddr_in address;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        close(server_fd);
        return EXIT_FAILURE;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(PORT);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }
    if (listen(server_fd, 16) == -1) {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }
    printf("сервер слушает порт %d\n", PORT);
    fflush(stdout);

    for (;;) {
        fd_set read_fds;
        int max_fd = server_fd;

        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        if (client_fd != -1) {
            FD_SET(client_fd, &read_fds);
            if (client_fd > max_fd)
                max_fd = client_fd;
        }

        if (pselect(max_fd + 1, &read_fds, NULL, NULL, NULL, NULL) == -1) {
            if (errno == EINTR)
                continue;
            perror("pselect");
            break;
        }

        if (FD_ISSET(server_fd, &read_fds)) {
            int new_fd = accept(server_fd, NULL, NULL);

            if (new_fd == -1) {
                perror("accept");
            } else if (client_fd == -1) {
                client_fd = new_fd;
                printf("новое соединение — оставлено открытым\n");
            } else {
                close(new_fd);
                printf("новое соединение — закрыто\n");
            }
            fflush(stdout);
        }

        if (client_fd != -1 && FD_ISSET(client_fd, &read_fds)) {
            char buffer[4096];
            ssize_t received = recv(client_fd, buffer, sizeof(buffer), 0);

            if (received > 0) {
                printf("получено байтов: %zd\n", received);
            } else {
                if (received == 0)
                    printf("клиент закрыл соединение\n");
                else
                    perror("recv");
                close(client_fd);
                client_fd = -1;
            }
            fflush(stdout);
        }
    }

    if (client_fd != -1)
        close(client_fd);
    close(server_fd);
    return EXIT_FAILURE;
}
