#!/bin/sh
# Сборка исходного файла на C во временном каталоге.
# Имя конечного файла задаётся комментарием «Output: имя» в исходнике.
#
# Использование: build.sh ИСХОДНЫЙ_ФАЙЛ

PROG=${0##*/}
WORKDIR=

die() {
    printf '%s: %s\n' "$PROG" "$*" >&2
    exit 1
}

trap '[ -n "$WORKDIR" ] && rm -rf -- "$WORKDIR"' EXIT

[ $# -eq 1 ] || die "использование: $PROG ИСХОДНЫЙ_ФАЙЛ"

SRC=$1
[ -f "$SRC" ] || die "файл не найден: $SRC"

SRC_DIR=$(cd -- "$(dirname -- "$SRC")" && pwd) || die "не удалось определить каталог $SRC"
SRC_NAME=${SRC##*/}
SRC_ABS=$SRC_DIR/$SRC_NAME

OUTPUT=$(sed -n 's/^.*Output:[[:space:]]*\([^[:space:]*]*\).*$/\1/p' "$SRC_ABS" | head -n 1)
[ -n "$OUTPUT" ] || die "в $SRC не найден комментарий 'Output: <имя файла>'"

WORKDIR=$(mktemp -d) || die "не удалось создать временный каталог"
cd -- "$WORKDIR" || die "не удалось перейти во временный каталог"

cc -Wall -O2 -o "$WORKDIR/result" "$SRC_ABS" || die "ошибка компиляции $SRC"

cp -- "$WORKDIR/result" "$SRC_DIR/$OUTPUT" || die "не удалось записать $SRC_DIR/$OUTPUT"
printf '%s: собран %s\n' "$PROG" "$SRC_DIR/$OUTPUT"
