#!/bin/sh
# Проверка отсутствия гонок: несколько раз снимает состояние общего тома
# и смотрит журнал ошибок, который ведут сами контейнеры.
# Использование: check.sh [число_снимков]   (по умолчанию 10)

SAMPLES=${1:-10}
VOLUME=shared-files

docker run --rm -v "$VOLUME:/shared" alpine:3.20 sh -c '
    bad=0
    s=1
    while [ "$s" -le '"$SAMPLES"' ]; do
        cd /shared || exit 1
        files=$(ls | grep -E "^[0-9]{3,}$" | wc -l)
        # Каждый файл должен содержать ровно одну строку «ID номер».
        for f in $(ls | grep -E "^[0-9]{3,}$"); do
            lines=$({ wc -l < "$f"; } 2>/dev/null) || continue   # файл успели удалить
            if [ "$lines" -gt 1 ]; then
                echo "снимок $s: в файле $f $lines строк(и)"; bad=1
            fi
        done
        # Пара «ID номер» не должна встречаться дважды.
        dup=$(cat [0-9][0-9][0-9]* 2>/dev/null | sort | uniq -d)
        [ -n "$dup" ] && { echo "снимок $s: дубликаты: $dup"; bad=1; }
        echo "снимок $s: файлов $files"
        s=$((s + 1))
        sleep 1
    done
    if [ -s /shared/.errors ]; then
        echo "Журнал ошибок контейнеров:"; cat /shared/.errors; bad=1
    fi
    [ "$bad" -eq 0 ] && echo "OK: гонок не обнаружено" || echo "FAIL"
    exit $bad
'
