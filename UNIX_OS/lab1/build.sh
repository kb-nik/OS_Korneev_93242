#!/bin/sh
# Сборка исходного файла (C или C++) во временном каталоге.
#
# Имя конечного файла задаётся в тексте исходника комментарием с ключевым
# словом «Output:», например:
#     // Output: hello            (C/C++)
#     /* Output: hello */         (C/C++)
#
# Использование: build.sh ИСХОДНЫЙ_ФАЙЛ
#
# Коды завершения:
#   0   успех
#   1   неверные аргументы
#   2   исходный файл не существует или недоступен на чтение
#   3   в исходнике нет комментария «Output:» или имя в нём некорректно
#   4   неподдерживаемый тип исходного файла
#   5   не найден компилятор
#   6   ошибка компиляции
#   7   не удалось создать временный каталог
#   8   не удалось скопировать результат рядом с исходником
#   128+N  работа прервана сигналом N (129 HUP, 130 INT, 131 QUIT, 141 PIPE, 143 TERM)

PROG=${0##*/}

E_USAGE=1
E_SOURCE=2
E_NO_OUTPUT=3
E_TYPE=4
E_NO_COMPILER=5
E_COMPILE=6
E_TMPDIR=7
E_COPY=8

WORKDIR=

die() {
    code=$1
    shift
    printf '%s: %s\n' "$PROG" "$*" >&2
    exit "$code"
}

cleanup() {
    if [ -n "$WORKDIR" ] && [ -d "$WORKDIR" ]; then
        rm -rf -- "$WORKDIR"
    fi
}

# EXIT срабатывает при любом завершении, в том числе при exit из обработчиков
# сигналов ниже, поэтому каталог удаляется при любом исходе.
trap 'rc=$?; trap - EXIT; cleanup; exit $rc' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 131' QUIT
trap 'exit 141' PIPE
trap 'exit 143' TERM

[ $# -eq 1 ] || die "$E_USAGE" "использование: $PROG ИСХОДНЫЙ_ФАЙЛ"

SRC=$1
[ -f "$SRC" ] || die "$E_SOURCE" "файл не найден: $SRC"
[ -r "$SRC" ] || die "$E_SOURCE" "файл недоступен на чтение: $SRC"

SRC_DIR=$(cd -- "$(dirname -- "$SRC")" && pwd) || die "$E_SOURCE" "не удалось определить каталог $SRC"
SRC_NAME=${SRC##*/}
SRC_ABS=$SRC_DIR/$SRC_NAME

# Ищем первый комментарий «Output: имя». Символ * исключён из имени,
# чтобы корректно разбирать вариант /* Output: name */.
OUTPUT=$(sed -n 's/^.*Output:[[:space:]]*\([^[:space:]*]*\).*$/\1/p' "$SRC_ABS" | head -n 1)
[ -n "$OUTPUT" ] || die "$E_NO_OUTPUT" "в $SRC не найден комментарий 'Output: <имя файла>'"
case $OUTPUT in
    */* | . | ..) die "$E_NO_OUTPUT" "некорректное имя конечного файла: '$OUTPUT'" ;;
esac

case $SRC_NAME in
    *.c)                             LANG_KIND=c ;;
    *.cpp | *.cc | *.cxx | *.C)      LANG_KIND=cxx ;;
    *) die "$E_TYPE" "неподдерживаемый тип файла: $SRC_NAME (ожидается .c, .cpp, .cc или .cxx)" ;;
esac

case $LANG_KIND in
    c)   COMPILER=${CC:-cc} ;;
    cxx) COMPILER=${CXX:-c++} ;;
esac
command -v "$COMPILER" >/dev/null 2>&1 || die "$E_NO_COMPILER" "компилятор не найден: $COMPILER"

WORKDIR=$(mktemp -d "${TMPDIR:-/tmp}/build.XXXXXX") || die "$E_TMPDIR" "не удалось создать временный каталог"

# Все промежуточные файлы (объектные, .aux, .log и т. п.) создаются в WORKDIR.
cd -- "$WORKDIR" || die "$E_TMPDIR" "не удалось перейти во временный каталог"

case $LANG_KIND in
    c | cxx)
        "$COMPILER" -Wall -Wextra -O2 -o "$WORKDIR/result" "$SRC_ABS" \
            || die "$E_COMPILE" "ошибка компиляции $SRC"
        ;;
esac

[ -f "$WORKDIR/result" ] || die "$E_COMPILE" "компилятор не создал конечный файл"

cp -- "$WORKDIR/result" "$SRC_DIR/$OUTPUT" \
    || die "$E_COPY" "не удалось записать $SRC_DIR/$OUTPUT"

printf '%s: собран %s\n' "$PROG" "$SRC_DIR/$OUTPUT"
exit 0
