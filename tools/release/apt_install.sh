#!/usr/bin/env bash
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Installs apt packages on a CI runner without letting a stalled mirror eat the job's timeout.
# apt-get has no overall deadline: on 2026-11-10 an Azure Ubuntu mirror stopped answering mid
# "apt-get update" and the nightly cook job sat silent for 59 minutes until it was cancelled.
# Every network read here times out after 30 s, each apt call is capped, and the index update is
# retried, so a bad mirror fails or recovers within minutes.
#
# Usage: tools/release/apt_install.sh <package>...
set -euo pipefail

if [ "$#" -eq 0 ]; then
	echo "usage: $0 <package>..." >&2
	exit 2
fi

apt_opts=(
	-o Acquire::Retries=3
	-o Acquire::http::Timeout=30
	-o Acquire::https::Timeout=30
)

attempts=3
for attempt in $(seq 1 "$attempts"); do
	if sudo timeout 180 apt-get "${apt_opts[@]}" update; then
		break
	fi
	if [ "$attempt" -eq "$attempts" ]; then
		echo "::error::apt-get update failed $attempts times"
		exit 1
	fi
	echo "::warning::apt-get update attempt $attempt failed, retrying"
	sleep 15
done

sudo timeout 600 apt-get "${apt_opts[@]}" install -y "$@"
