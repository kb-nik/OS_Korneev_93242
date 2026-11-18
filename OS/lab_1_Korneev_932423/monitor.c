#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct monitor {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    bool ready;
    bool stopping;
    int *data;
};

static struct monitor mon = {
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    false,
    false,
    NULL
};

static void check(int rc, const char *what)
{
    if (rc != 0) {
        fprintf(stderr, "%s: %s\n", what, strerror(rc));
        exit(EXIT_FAILURE);
    }
}

static void provide(struct monitor *monitor, int value)
{
    int *event;

    check(pthread_mutex_lock(&monitor->mutex), "pthread_mutex_lock");
    while (monitor->ready) {
        check(pthread_mutex_unlock(&monitor->mutex), "pthread_mutex_unlock");
        sleep(1);
        check(pthread_mutex_lock(&monitor->mutex), "pthread_mutex_lock");
    }

    event = malloc(sizeof(*event));
    if (event == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    *event = value;
    monitor->data = event;
    monitor->ready = true;
    printf("поставщик: отправлено событие %d\n", value);
    fflush(stdout);
    check(pthread_cond_signal(&monitor->condition), "pthread_cond_signal");
    check(pthread_mutex_unlock(&monitor->mutex), "pthread_mutex_unlock");
}

static bool consume(struct monitor *monitor)
{
    int *event;

    check(pthread_mutex_lock(&monitor->mutex), "pthread_mutex_lock");
    while (!monitor->ready && !monitor->stopping)
        check(pthread_cond_wait(&monitor->condition, &monitor->mutex),
              "pthread_cond_wait");

    if (monitor->stopping) {
        check(pthread_mutex_unlock(&monitor->mutex), "pthread_mutex_unlock");
        return false;
    }

    event = monitor->data;
    monitor->data = NULL;
    monitor->ready = false;
    printf("потребитель: получено событие %d\n", *event);
    fflush(stdout);
    free(event);
    check(pthread_cond_signal(&monitor->condition), "pthread_cond_signal");
    check(pthread_mutex_unlock(&monitor->mutex), "pthread_mutex_unlock");
    return true;
}

static void finish(struct monitor *monitor)
{
    check(pthread_mutex_lock(&monitor->mutex), "pthread_mutex_lock");
    while (monitor->ready)
        check(pthread_cond_wait(&monitor->condition, &monitor->mutex),
              "pthread_cond_wait");
    monitor->stopping = true;
    check(pthread_cond_broadcast(&monitor->condition), "pthread_cond_broadcast");
    check(pthread_mutex_unlock(&monitor->mutex), "pthread_mutex_unlock");
}

static void *producer(void *arg)
{
    int count = *(int *)arg;
    int id = 1;

    while (count == 0 || id <= count) {
        sleep(1);
        provide(&mon, id * 10);
        id = id == INT_MAX / 10 ? 1 : id + 1;
    }
    finish(&mon);
    return NULL;
}

static void *consumer(void *arg)
{
    (void)arg;
    while (consume(&mon)) {
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    pthread_t prod, cons;
    int count = 5;

    if (argc > 2) {
        fprintf(stderr, "использование: %s [число_событий]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (argc == 2) {
        char *end;
        long n = strtol(argv[1], &end, 10);
        if (*argv[1] == '\0' || *end != '\0' || n < 0 || n > 1000000) {
            fprintf(stderr, "некорректное число событий: %s\n", argv[1]);
            return EXIT_FAILURE;
        }
        count = (int)n;
    }

    check(pthread_create(&cons, NULL, consumer, NULL), "pthread_create");
    check(pthread_create(&prod, NULL, producer, &count), "pthread_create");
    check(pthread_join(prod, NULL), "pthread_join");
    check(pthread_join(cons, NULL), "pthread_join");

    check(pthread_cond_destroy(&mon.condition), "pthread_cond_destroy");
    check(pthread_mutex_destroy(&mon.mutex), "pthread_mutex_destroy");
    return EXIT_SUCCESS;
}
