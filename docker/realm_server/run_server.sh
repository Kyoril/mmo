#!/bin/bash

# Create a configuration file from environment variables
cat > /app/config/realm_server.cfg << EOF
version = 1

mysqlDatabase = 
(
	port = 3306
    host = "${MYSQL_HOST_NAME}"
    user = "${MYSQL_USER_NAME}"
    password = "${MYSQL_PASSWORD}"
    database = "${MYSQL_DATABASE}"
)

realmConfig = 
(
	loginServerAddress = "${LOGIN_HOST_NAME}"
	loginServerPort = 6279
	realmName = "${LOGIN_REALM_NAME}"
	realmPasswordHash = "${LOGIN_REALM_PASSWORD_HASH0}"
)

webServer = 
(
	port = 8092
	ssl_port = 8093
    user = "${WEB_API_USER}"
    password = "${WEB_API_PASSWORD}"
)

playerManager = 
(
	port = 8130
	maxCount = 20
)

worldManager = 
(
	port = 6280
	maxCount = 255
)

log = 
(
	active = 1
	fileName = "logs/realm_server"
	buffering = 0
)
EOF

# Start the server
./realm_server
