#!/bin/bash

# Create a configuration file from environment variables
cat > /app/config/world_server.cfg << EOF
version = 1

mysqlDatabase = 
(
	port = 3306
    host = "${MYSQL_HOST_NAME}"
    user = "${MYSQL_USER_NAME}"
    password = "${MYSQL_PASSWORD}"
    database = "${MYSQL_DATABASE}"
)

worldConfig = 
(
	realmServerAddress = "${REALM_HOST_NAME}"
	realmServerPort = ${REALM_WORLD_PORT}
	realmServerAuthName = "${REALM_WORLD_NAME}"
	realmServerPassword = "${REALM_WORLD_PASSWORD_HASH}"
	hostedMaps = {0}
)

webServer = 
(
	port = 8094
	ssl_port = 8095
	user = "${WEB_API_USER}"
	password = "${WEB_API_PASSWORD}"
)

playerManager = 
(
	maxCount = 100
)

log = 
(
	active = 1
	fileName = "logs/logs"
	buffering = 0
)
EOF

# Start the server
./world_server
