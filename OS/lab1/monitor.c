/*
 * Монитор с двумя потоками: поставщиком и потребителем.
 *
 * Поставщик раз в секунду создаёт событие и передаёт его в монитор,
 * потребитель ждёт событие на условной переменной и обрабатывает его.
 * Событие передаётся по указателю (struct event выделяется в куче
 * поставщиком и освобождается потребителем), т. е. данные не копируются
 * и не сериализуются.
 *
 * Использование: ./monitor [число_событий]   (0 — бесконечно, по умолчанию 10)
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
    int stopping;               /* поставщик закончил работу */
    struct event *data;
};

static struct monitor mon = {
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    0,
    0,
    NULL
};

static void check(int rc, const char *what)
{
    if (rc != 0) {
        fprintf(stderr, "%s: %s\n", what, strerror(rc));
        exit(EXIT_FAILURE);
    }
}

/*
 * Передать событие в монитор. Если предыдущее событие ещё не обработано,
 * поставщик ждёт на free_cond, а не опрашивает ready в цикле — так он тоже
 * не тратит процессорное время.
 */
static void provide(struct event *ev)
{
    check(pthread_mutex_lock(&mon.mutex), "pthread_mutex_lock");
    while (mon.ready)
        check(pthread_cond_wait(&mon.free_cond, &mon.mutex), "pthread_cond_wait");
    mon.data = ev;
    mon.ready = 1;
    /* Сообщение печатается под мьютексом, поэтому порядок сообщений
     * совпадает с порядком переходов состояния монитора. */
    printf("поставщик: отправлено событие %d\n", ev->id);
    fflush(stdout);
    check(pthread_cond_signal(&mon.ready_cond), "pthread_cond_signal");
    check(pthread_mutex_unlock(&mon.mutex), "pthread_mutex_unlock");
}

/* Сообщить потребителю, что событий больше не будет. */
static void stop(void)
{
    check(pthread_mutex_lock(&mon.mutex), "pthread_mutex_lock");
    mon.stopping = 1;
    check(pthread_cond_broadcast(&mon.ready_cond), "pthread_cond_broadcast");
    check(pthread_mutex_unlock(&mon.mutex), "pthread_mutex_unlock");
}

/*
 * Получить событие. Условие проверяется в цикле: пробуждение может быть
 * ложным, а уведомление могло прийти раньше, чем поток начал ждать.
 * Возвращает NULL, когда поставщик завершил работу и событий не осталось.
 */
static struct event *consume(void)
{
    struct event *ev = NULL;

    check(pthread_mutex_lock(&mon.mutex), "pthread_mutex_lock");
    while (!mon.ready && !mon.stopping)
        check(pthread_cond_wait(&mon.ready_cond, &mon.mutex), "pthread_cond_wait");
    if (mon.ready) {
        ev = mon.data;
        mon.data = NULL;
        mon.ready = 0;
        printf("потребитель: получено событие %d (%s)\n", ev->id, ev->text);
        fflush(stdout);
        check(pthread_cond_signal(&mon.free_cond), "pthread_cond_signal");
    }
    check(pthread_mutex_unlock(&mon.mutex), "pthread_mutex_unlock");
    return ev;
}

static void *producer(void *arg)
{
    int count = *(int *)arg;
    int id;

    for (id = 1; count == 0 || id <= count; id++) {
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
    stop();
    return NULL;
}

static void *consumer(void *arg)
{
    struct event *ev;

    (void)arg;
    while ((ev = consume()) != NULL) {
        /* Обработка вне мьютекса: поставщик в это время может готовить
         * следующее событие. Владение событием перешло к потребителю. */
        free(ev->text);
        free(ev);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    pthread_t prod, cons;
    int count = 10;

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

    /* Оба потока завершены — монитор больше никто не использует. */
    check(pthread_cond_destroy(&mon.ready_cond), "pthread_cond_destroy");
    check(pthread_cond_destroy(&mon.free_cond), "pthread_cond_destroy");
    check(pthread_mutex_destroy(&mon.mutex), "pthread_mutex_destroy");
    return EXIT_SUCCESS;
}
