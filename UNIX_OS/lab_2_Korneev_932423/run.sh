#!/bin/sh
# Сборка образа, создание общего тома и запуск N экземпляров контейнера.
# Использование: run.sh [N]   (по умолчанию N=1)

N=${1:-1}
IMAGE=concurrent-files:1.0
VOLUME=shared-files
PREFIX=cfworker

cd -- "$(dirname -- "$0")" || exit 1

docker build -q -t "$IMAGE" . >/dev/null || exit 1
docker volume inspect "$VOLUME" >/dev/null 2>&1 || docker volume create "$VOLUME" >/dev/null || exit 1

i=1
while [ "$i" -le "$N" ]; do
    docker run -d --name "$PREFIX-$i" -v "$VOLUME:/shared" "$IMAGE" >/dev/null || exit 1
    i=$((i + 1))
done
echo "Запущено контейнеров: $N (том $VOLUME)"
