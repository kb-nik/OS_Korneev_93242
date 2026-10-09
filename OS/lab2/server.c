/*
 * TCP-сервер на pselect().
 *
 * - Сообщает о каждом новом подключении. Первое соединение остаётся
 *   открытым, остальные закрываются сразу после принятия.
 * - При появлении данных в сохранённом соединении выводит их количество.
 * - При получении SIGHUP выводит сообщение и продолжает работу.
 *
 * SIGHUP заблокирован везде, кроме самого pselect(): ядро атомарно снимает
 * блокировку на время ожидания, поэтому сигнал не может прийти «между
 * проверкой флага и началом ожидания» и потеряться.
 */

#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 12345

/* Обработчик только фиксирует факт сигнала, вся работа — в основном цикле. */
static volatile sig_atomic_t was_sighup = 0;

static void sighup_handler(int signal_number)
{
    (void)signal_number;
    was_sighup = 1;
}

int main(void)
{
    int server_fd;
    int client_fd = -1;
    int opt = 1;
    struct sockaddr_in address;
    struct sigaction action;
    sigset_t blocked_mask, original_mask;

    memset(&action, 0, sizeof(action));
    action.sa_handler = sighup_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    if (sigaction(SIGHUP, &action, NULL) == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    sigemptyset(&blocked_mask);
    sigaddset(&blocked_mask, SIGHUP);
    if (sigprocmask(SIG_BLOCK, &blocked_mask, &original_mask) == -1) {
        perror("sigprocmask");
        return EXIT_FAILURE;
    }

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

        if (pselect(max_fd + 1, &read_fds, NULL, NULL, NULL, &original_mask) == -1) {
            if (errno != EINTR) {
                perror("pselect");
                break;
            }
            /* Прервано сигналом: наборы дескрипторов не определены,
             * обрабатываем только флаг. */
            if (was_sighup) {
                was_sighup = 0;
                printf("получен сигнал SIGHUP\n");
                fflush(stdout);
            }
            continue;
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
