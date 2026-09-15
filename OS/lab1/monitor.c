/*
 * Монитор с двумя потоками: поставщиком и потребителем.
 *
 * Поставщик раз в секунду создаёт событие и передаёт его в монитор,
 * потребитель ждёт событие на условной переменной и обрабатывает его.
 * Событие передаётся по указателю.
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct event {
    int id;
    char *text;
};

pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
int ready = 0;
struct event *data = NULL;

static void provide(struct event *ev)
{
    pthread_mutex_lock(&mutex);
    data = ev;
    ready = 1;
    printf("поставщик: отправлено событие %d\n", ev->id);
    fflush(stdout);
    pthread_cond_signal(&condition);
    pthread_mutex_unlock(&mutex);
}

static struct event *consume(void)
{
    struct event *ev;

    pthread_mutex_lock(&mutex);
    /* Условие проверяется в цикле: пробуждение может быть ложным. */
    while (!ready)
        pthread_cond_wait(&condition, &mutex);
    ev = data;
    data = NULL;
    ready = 0;
    printf("потребитель: получено событие %d (%s)\n", ev->id, ev->text);
    fflush(stdout);
    pthread_mutex_unlock(&mutex);
    return ev;
}

static void *producer(void *arg)
{
    int id;

    (void)arg;
    for (id = 1;; id++) {
        struct event *ev;
        char text[64];

        sleep(1);
        ev = malloc(sizeof(*ev));
        snprintf(text, sizeof(text), "данные события №%d", id);
        ev->text = strdup(text);
        ev->id = id;
        provide(ev);
    }
    return NULL;
}

static void *consumer(void *arg)
{
    (void)arg;
    for (;;) {
        struct event *ev = consume();
        free(ev->text);
        free(ev);
    }
    return NULL;
}

int main(void)
{
    pthread_t prod, cons;

    pthread_create(&cons, NULL, consumer, NULL);
    pthread_create(&prod, NULL, producer, NULL);
    pthread_join(prod, NULL);
    pthread_join(cons, NULL);
    return EXIT_SUCCESS;
}
