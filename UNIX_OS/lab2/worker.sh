#!/bin/sh
# Конкурентное создание и удаление файлов в общем каталоге.
#
# В бесконечном цикле с паузой в одну секунду между операциями:
#   1) под блокировкой flock выбирается первое свободное имя из 001, 002, ...
#      и создаётся файл с идентификатором контейнера и порядковым номером;
#   2) созданный файл удаляется.

DIR=${SHARED_DIR:-/shared}
LOCK=$DIR/.lock

ID=$(head -c 8 /dev/urandom | od -An -tx1 | tr -d ' \n')

mkdir -p -- "$DIR" || exit 1
exec 9>>"$LOCK" || exit 1

echo "[$ID] запущен, каталог $DIR"
n=0
while :; do
    n=$((n + 1))

    # ---- атомарный участок: поиск имени и создание файла ----
    flock -x 9 || exit 1
    i=1
    while :; do
        name=$(printf '%03d' "$i")
        [ -e "$DIR/$name" ] || break
        i=$((i + 1))
    done
    printf '%s %d\n' "$ID" "$n" > "$DIR/$name"
    flock -u 9
    # ---- конец атомарного участка ----

    echo "[$ID] создан $name"
    sleep 1
    rm -f -- "$DIR/$name"
    echo "[$ID] удалён $name"
    sleep 1
done
