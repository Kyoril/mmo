FROM ubuntu:18.04

# Install dependencies necessary to run login server
RUN apt-get update && apt-get install -y \
    libssl-dev \
    libmysqlclient-dev

RUN mkdir -p /app/config

COPY --from=builder /app/bin/login_server /app
COPY --from=builder /app/docker/login_server/run_server.sh /app
COPY --from=builder /app/docker/login_server/login_server.cfg /app/config/login_server.cfg

WORKDIR /app

ENTRYPOINT [ "/app/run_server.sh" ]
