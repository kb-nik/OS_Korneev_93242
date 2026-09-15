CC = gcc
CFLAGS = -Wall -Wextra -pedantic -O2 -pthread

all: monitor

monitor: monitor.c
	$(CC) $(CFLAGS) -o monitor monitor.c

clean:
	rm -f monitor

.PHONY: all clean
