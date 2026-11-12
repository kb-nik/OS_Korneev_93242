#!/bin/sh
# Остановка и удаление всех запущенных экземпляров.
# С ключом --purge удаляется и общий том.

PREFIX=cfworker
VOLUME=shared-files

ids=$(docker ps -aq --filter "name=^$PREFIX-")
[ -n "$ids" ] && docker rm -f $ids >/dev/null
[ "$1" = "--purge" ] && docker volume rm "$VOLUME" >/dev/null 2>&1
echo "Остановлено"
