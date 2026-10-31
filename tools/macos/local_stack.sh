#!/bin/bash
# Starts a local login + realm + world server stack for playing the macOS client against, with a
# private throwaway MySQL server (no system MySQL needed). Mirrors tools/e2e/e2e_up.ps1.
#
#   tools/macos/local_stack.sh up      # build login_server, realm_server, world_server first
#   tools/macos/local_stack.sh down
#
# Account E2EGM / E2EGMPASS (GM level 3). The client always connects to port 3724; start it with
#   bin/mmo_client.app/Contents/MacOS/mmo_client -exec tools/macos/local_login.cfg
# Create a character in the client, or with the headless e2e_client (see e2e/README.md).
set -e
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
RT="${MMO_LOCAL_STACK_DIR:-$REPO/e2e/runtime/macos}"
BIN=$REPO/bin
MYSQLD=/opt/homebrew/opt/mysql/bin/mysqld
MYSQL="/opt/homebrew/opt/mysql/bin/mysql -uroot -h127.0.0.1 -P3307"

# The servers shut down on SIGINT; anything still running after a few seconds is killed.
stop_pid()
{
	[ -f "$1" ] || return 0
	local pid; pid=$(cat "$1")
	kill -INT "$pid" 2>/dev/null || true
	for i in 1 2 3 4 5; do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
	kill -9 "$pid" 2>/dev/null || true
	rm -f "$1"
}

if [ "$1" = "down" ]; then
	stop_pid $RT/world.pid; stop_pid $RT/realm.pid; stop_pid $RT/login.pid; stop_pid $RT/mysql/mysqld.pid
	echo "stack stopped"; exit 0
fi

mkdir -p $RT/mysql
if [ ! -d $RT/mysql/data ]; then
	$MYSQLD --no-defaults --initialize-insecure --datadir=$RT/mysql/data --basedir=/opt/homebrew/opt/mysql > $RT/mysql/init.log 2>&1
fi
if ! $MYSQL -e "select 1" > /dev/null 2>&1; then
	# Relative socket path: Unix socket paths are limited to ~100 characters.
	(cd $RT/mysql && nohup $MYSQLD --no-defaults --datadir=$RT/mysql/data --basedir=/opt/homebrew/opt/mysql --port=3307 --bind-address=127.0.0.1 --socket=mysql.sock --mysqlx=OFF --log-error=$RT/mysql/error.log --pid-file=$RT/mysql/mysqld.pid > /dev/null 2>&1 &)
	for i in $(seq 1 30); do $MYSQL -e "select 1" > /dev/null 2>&1 && break; sleep 1; done
fi
WEBUSER=mmo-e2e
WEBPASS=e2e-secret
REALM=E2EREALM
REALMPASS=E2EREALMPASS
WORLD=E2EWORLD
WORLDPASS=E2EWORLDPASS
ACCOUNT=E2EGM
ACCOUNTPASS=E2EGMPASS

sha1() { printf '%s' "$1" | shasum -a 1 | cut -d' ' -f1 | tr 'a-z' 'A-Z'; }

mkdir -p $RT/config $RT/login $RT/realm $RT/world $RT/crashes $RT/bugs
$MYSQL -e "DROP DATABASE IF EXISTS mmo_login_e2e; CREATE DATABASE mmo_login_e2e; DROP DATABASE IF EXISTS mmo_realm_e2e; CREATE DATABASE mmo_realm_e2e; DROP DATABASE IF EXISTS mmo_world_e2e; CREATE DATABASE mmo_world_e2e;"
grep -v -E '^[[:space:]]*USE[[:space:]]' $REPO/data/login/login_db_schema_full.sql | $MYSQL mmo_login_e2e
grep -v -E '^[[:space:]]*USE[[:space:]]' $REPO/data/realm/realm_db_schema_full.sql | $MYSQL mmo_realm_e2e

REALMHASH=$(sha1 "$REALM:$REALMPASS")
WORLDHASH=$(sha1 "$WORLD:$WORLDPASS")

cat > $RT/config/login_server.cfg <<EOF
version = 1
mysqlDatabase =
(
	port = 3307
	host = "127.0.0.1"
	user = "root"
	password = ""
	database = "mmo_login_e2e"
	updatePath = "$REPO/data/login/updates"
	poolSize = 4
)
webServer =
(
	port = 18090
	ssl_port = 18091
	user = "$WEBUSER"
	password = "$WEBPASS"
)
playerManager =
(
	port = 3724
	maxCount = 100
)
realmManager =
(
	port = 16279
	maxCount = 16
)
log =
(
	active = 1
	fileName = "logs/login"
	buffering = 0
)
crashReport =
(
	enabled = 0
	apiUrl = "file://$RT/crashes"
)
EOF

cat > $RT/config/realm_server.cfg <<EOF
version = 1
mysqlDatabase =
(
	port = 3307
	host = "127.0.0.1"
	user = "root"
	password = ""
	database = "mmo_realm_e2e"
	updatePath = "$REPO/data/realm/updates"
	poolSize = 4
)
realmConfig =
(
	loginServerAddress = "127.0.0.1"
	loginServerPort = 16279
	realmName = "$REALM"
	realmPasswordHash = "$REALMHASH"
)
webServer =
(
	port = 18092
	ssl_port = 18093
	user = "$WEBUSER"
	password = "$WEBPASS"
)
playerManager =
(
	port = 18129
	maxCount = 20
)
worldManager =
(
	port = 16280
	maxCount = 16
)
folders =
(
	data = "$REPO/data/editor/data"
)
log =
(
	active = 1
	fileName = "logs/realm"
	buffering = 0
)
crashReport =
(
	enabled = 0
	apiUrl = "file://$RT/crashes"
)
EOF

cat > $RT/config/world_server.cfg <<EOF
version = 1
mysqlDatabase =
(
	port = 3307
	host = "127.0.0.1"
	user = "root"
	password = ""
	database = "mmo_world_e2e"
)
worldConfig =
(
	realmServerAddress = "127.0.0.1"
	realmServerPort = 16280
	realmServerAuthName = "$WORLD"
	realmServerPassword = "$WORLDHASH"
	hostedMaps = {0,1}
)
webServer =
(
	port = 18094
	ssl_port = 18095
	user = "$WEBUSER"
	password = "$WEBPASS"
)
playerManager =
(
	maxCount = 100
)
folders =
(
	data = "$REPO/data/editor/data"
	maps = "$REPO/data/editor/nav"
	worldData = "$REPO/data/client"
	scripts = "$REPO/data/scripts"
)
bugReport =
(
	enabled = 0
	apiUrl = "file://$RT/bugs"
	apiKey = ""
)
log =
(
	active = 1
	fileName = "logs/world"
	buffering = 0
)
crashReport =
(
	enabled = 0
	apiUrl = "file://$RT/crashes"
)
EOF

wait_http() {
	for i in $(seq 1 60); do
		if curl -s -u "$WEBUSER:$WEBPASS" "$1" > /dev/null; then return 0; fi
		sleep 1
	done
	echo "timeout waiting for $1"; return 1
}

post() { curl -s -u "$WEBUSER:$WEBPASS" -X POST "$@"; echo; }

cd $RT/login
$BIN/login_server -c $RT/config/login_server.cfg < /dev/null > $RT/login/out.log 2>&1 &
echo $! > $RT/login.pid
wait_http http://127.0.0.1:18090/uptime
post http://127.0.0.1:18090/create-realm -d "id=$REALM" -d "password=$REALMPASS" -d "address=127.0.0.1" -d "port=18129"
post http://127.0.0.1:18090/create-account -d "id=$ACCOUNT" -d "password=$ACCOUNTPASS"
post http://127.0.0.1:18090/gm-level -d "account_name=$ACCOUNT" -d "gm_level=3"

cd $RT/realm
$BIN/realm_server -c $RT/config/realm_server.cfg < /dev/null > $RT/realm/out.log 2>&1 &
echo $! > $RT/realm.pid
wait_http http://127.0.0.1:18092/uptime
sleep 3
post http://127.0.0.1:18092/create-world -d "id=$WORLD" -d "password=$WORLDPASS"

cd $RT/world
$BIN/world_server -c $RT/config/world_server.cfg < /dev/null > $RT/world/out.log 2>&1 &
echo $! > $RT/world.pid
for i in $(seq 1 90); do
	if grep -q "Successfully authenticated at the realm server" "$RT"/world/logs/*.log 2>/dev/null; then echo "world up"; exit 0; fi
	sleep 1
done
echo "world did not authenticate"; exit 1
