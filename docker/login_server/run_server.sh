#!/bin/bash

# Create a configuration file from environment variables
cat > /app/config/login_server.cfg << EOF
version = 1

mysqlDatabase = 
(
        port = 3306
        host = "${MYSQL_HOST_NAME}"
        user = "${MYSQL_USER_NAME}"
        password = "${MYSQL_PASSWORD}"
        database = "${MYSQL_DATABASE}"
)

webServer = 
(
        port = 8090
        ssl_port = 8091
        user = "${LOGIN_WEB_API_USER}"
        password = "${LOGIN_WEB_API_PASSWORD}"
)

playerManager = 
(
        port = 3724
        maxCount = 1000
)

realmManager = 
(
        port = 6279
        maxCount = 255
)

log = 
(
        active = 1
        fileName = "logs/logs"
        buffering = 0
)
EOF

# Start the server
./login_server
