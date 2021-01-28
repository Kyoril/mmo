FROM ubuntu:18.04

# Install dependencies necessary to run world server
RUN apt-get update && apt-get install -y \
    libssl-dev \
    libmysqlclient-dev

RUN mkdir -p /app/config

COPY --from=builder /app/bin/world_server /app
COPY --from=builder /app/docker/world_server/run_server.sh /app
COPY --from=builder /app/docker/world_server/world_server.cfg /app/config/world_server.cfg

WORKDIR /app

ENTRYPOINT [ "/app/run_server.sh" ]
