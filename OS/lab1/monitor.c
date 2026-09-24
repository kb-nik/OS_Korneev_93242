/*
 * Монитор с двумя потоками: поставщиком и потребителем.
 *
 * Поставщик раз в секунду создаёт событие и передаёт его в монитор,
 * потребитель ждёт событие на условной переменной и обрабатывает его.
 * Событие передаётся по указателю (struct event выделяется в куче
 * поставщиком и освобождается потребителем), т. е. данные не копируются
 * и не сериализуются.
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Условное событие: несериализуемые данные, передаются по указателю. */
struct event {
    int id;
    char *text;
};

/* Монитор: разделяемое состояние, блокировка и ожидание уведомления. */
struct monitor {
    pthread_mutex_t mutex;
    pthread_cond_t ready_cond;  /* событие появилось — для потребителя */
    pthread_cond_t free_cond;   /* событие обработано — для поставщика */
    int ready;                  /* предикат: в мониторе есть необработанное событие */
    struct event *data;
};

static struct monitor mon = {
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    0,
    NULL
};

/*
 * Передать событие в монитор. Если предыдущее событие ещё не обработано,
 * поставщик ждёт на free_cond, а не опрашивает ready в цикле — так он тоже
 * не тратит процессорное время.
 */
static void provide(struct event *ev)
{
    pthread_mutex_lock(&mon.mutex);
    while (mon.ready)
        pthread_cond_wait(&mon.free_cond, &mon.mutex);
    mon.data = ev;
    mon.ready = 1;
    /* Сообщение печатается под мьютексом, поэтому порядок сообщений
     * совпадает с порядком переходов состояния монитора. */
    printf("поставщик: отправлено событие %d\n", ev->id);
    fflush(stdout);
    pthread_cond_signal(&mon.ready_cond);
    pthread_mutex_unlock(&mon.mutex);
}

/*
 * Получить событие. Условие проверяется в цикле: пробуждение может быть
 * ложным, а уведомление могло прийти раньше, чем поток начал ждать.
 */
static struct event *consume(void)
{
    struct event *ev;

    pthread_mutex_lock(&mon.mutex);
    while (!mon.ready)
        pthread_cond_wait(&mon.ready_cond, &mon.mutex);
    ev = mon.data;
    mon.data = NULL;
    mon.ready = 0;
    printf("потребитель: получено событие %d (%s)\n", ev->id, ev->text);
    fflush(stdout);
    pthread_cond_signal(&mon.free_cond);
    pthread_mutex_unlock(&mon.mutex);
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
        if (ev == NULL || (ev->text = strdup(text)) == NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
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

        /* Обработка вне мьютекса: поставщик в это время может готовить
         * следующее событие. Владение событием перешло к потребителю. */
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
