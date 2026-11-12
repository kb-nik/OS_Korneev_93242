/*
 * TCP-сервер, совмещающий обработку соединений и сигналов в одном
 * ожидании pselect().
 *
 * - Сообщает о каждом новом подключении. Первое соединение остаётся
 *   открытым, остальные закрываются сразу после принятия.
 * - При появлении данных в сохранённом соединении выводит их количество.
 * - При получении SIGHUP выводит сообщение и продолжает работу.
 * - SIGINT и SIGTERM приводят к корректному завершению с закрытием сокетов.
 *
 * Все обрабатываемые сигналы заблокированы везде, кроме самого pselect():
 * ядро атомарно снимает блокировку на время ожидания, поэтому сигнал не
 * может прийти «между проверкой флага и началом ожидания» и потеряться.
 *
 * Использование: ./server [порт]   (по умолчанию 12345)
 */

#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

/* Обработчики только фиксируют факт сигнала, вся работа — в основном цикле. */
static volatile sig_atomic_t was_sighup = 0;
static volatile sig_atomic_t was_stop = 0;

static void sighup_handler(int signal_number)
{
    (void)signal_number;
    was_sighup = 1;
}

static void stop_handler(int signal_number)
{
    (void)signal_number;
    was_stop = 1;
}

static int set_handler(int signal_number, void (*handler)(int))
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    if (sigaction(signal_number, &action, NULL) == -1) {
        perror("sigaction");
        return -1;
    }
    return 0;
}

static int parse_port(const char *s)
{
    char *end;
    long port = strtol(s, &end, 10);

    if (*s == '\0' || *end != '\0' || port < 1 || port > 65535)
        return -1;
    return (int)port;
}

int main(int argc, char *argv[])
{
    int port = 12345;
    int server_fd = -1;
    int client_fd = -1;
    int opt = 1;
    int rc = EXIT_FAILURE;
    struct sockaddr_in address;
    sigset_t blocked_mask, original_mask, wait_mask;

    if (argc > 2) {
        fprintf(stderr, "использование: %s [порт]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (argc == 2 && (port = parse_port(argv[1])) == -1) {
        fprintf(stderr, "некорректный порт: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    /* Сигналы блокируются до создания сокета: с этого момента ни один
     * из них не будет доставлен вне pselect(). */
    sigemptyset(&blocked_mask);
    sigaddset(&blocked_mask, SIGHUP);
    sigaddset(&blocked_mask, SIGINT);
    sigaddset(&blocked_mask, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &blocked_mask, &original_mask) == -1) {
        perror("sigprocmask");
        return EXIT_FAILURE;
    }
    if (set_handler(SIGHUP, sighup_handler) == -1
        || set_handler(SIGINT, stop_handler) == -1
        || set_handler(SIGTERM, stop_handler) == -1)
        goto out;

    /* Маска на время ожидания: исходная, но с гарантированно
     * разблокированными обрабатываемыми сигналами. */
    wait_mask = original_mask;
    sigdelset(&wait_mask, SIGHUP);
    sigdelset(&wait_mask, SIGINT);
    sigdelset(&wait_mask, SIGTERM);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        goto out;
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        goto out;
    }
    /* Неблокирующий серверный сокет: если клиент отключится между pselect()
     * и accept(), accept() вернёт EAGAIN, а не повесит основной цикл. */
    if (fcntl(server_fd, F_SETFL, fcntl(server_fd, F_GETFL) | O_NONBLOCK) == -1) {
        perror("fcntl");
        goto out;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("bind");
        goto out;
    }
    if (listen(server_fd, 16) == -1) {
        perror("listen");
        goto out;
    }
    printf("сервер слушает порт %d, PID %ld\n", port, (long)getpid());
    fflush(stdout);

    while (!was_stop) {
        fd_set read_fds;
        int max_fd = server_fd;
        int ready;

        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        if (client_fd != -1) {
            FD_SET(client_fd, &read_fds);
            if (client_fd > max_fd)
                max_fd = client_fd;
        }

        ready = pselect(max_fd + 1, &read_fds, NULL, NULL, NULL, &wait_mask);
        if (ready == -1) {
            if (errno != EINTR) {
                perror("pselect");
                goto out;
            }
            /* Прервано сигналом: наборы дескрипторов не определены,
             * обрабатываем только флаги. */
            if (was_sighup) {
                was_sighup = 0;
                printf("получен сигнал SIGHUP\n");
                fflush(stdout);
            }
            continue;
        }

        if (FD_ISSET(server_fd, &read_fds)) {
            struct sockaddr_in peer;
            socklen_t peer_len = sizeof(peer);
            int new_fd = accept(server_fd, (struct sockaddr *)&peer, &peer_len);

            if (new_fd == -1) {
                if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ECONNABORTED
                    && errno != EINTR)
                    perror("accept");
            } else {
                char ip[INET_ADDRSTRLEN];

                inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
                if (client_fd == -1 && new_fd < FD_SETSIZE) {
                    client_fd = new_fd;
                    printf("новое соединение %s:%d — оставлено открытым\n",
                           ip, ntohs(peer.sin_port));
                } else {
                    close(new_fd);
                    printf("новое соединение %s:%d — закрыто\n",
                           ip, ntohs(peer.sin_port));
                }
                fflush(stdout);
            }
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

    printf("завершение работы\n");
    rc = EXIT_SUCCESS;

out:
    /* Освобождение в порядке, обратном созданию. */
    if (client_fd != -1)
        close(client_fd);
    if (server_fd != -1)
        close(server_fd);
    sigprocmask(SIG_SETMASK, &original_mask, NULL);
    return rc;
}
