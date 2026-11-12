FROM alpine:3.20

# flock, od, head, sleep предоставляются BusyBox из базового образа.
COPY worker.sh /usr/local/bin/worker.sh
RUN chmod +x /usr/local/bin/worker.sh

ENV SHARED_DIR=/shared
VOLUME ["/shared"]

ENTRYPOINT ["/usr/local/bin/worker.sh"]
