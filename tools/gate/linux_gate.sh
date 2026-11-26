#!/usr/bin/env bash
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# The Linux half of the merge gate. verify.ps1 runs it inside WSL on Windows so that a change
# which only breaks gcc/libstdc++ (the servers ship on Linux) is caught at /ship time and not
# by the nightly. It builds the gate targets with the CI toolchain (gcc-13, Ninja Multi-Config),
# runs every CTest suite and the deployer tests, whose symlink tests only run on Linux.
#
# The source is copied into WSL's own filesystem first: building across /mnt is several times
# slower, and the copy keeps one incremental build per checkout.
#
# Usage: linux_gate.sh <checkout as a WSL path> <target>...
# Exit:  0 green, 1 red, 2 the WSL toolchain is missing.

set -uo pipefail

# Windows' PATH is appended inside WSL; a cmake.exe found there must never be picked up.
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

source_dir="${1:?usage: linux_gate.sh <checkout> <target>...}"
shift
targets=("$@")

missing=()
for tool in gcc-13 g++-13 cmake ninja rsync python3 flock; do
	command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
done
for header in mysql/mysql.h uuid/uuid.h openssl/ssl.h; do
	[ -f "/usr/include/$header" ] || missing+=("$header")
done
if [ ${#missing[@]} -ne 0 ]; then
	echo "The WSL toolchain for the Linux gate is incomplete; missing: ${missing[*]}"
	echo "Install it once inside WSL (Ubuntu 24.04, like the CI runner):"
	echo "  sudo apt-get update && sudo apt-get install -y gcc-13 g++-13 cmake ninja-build rsync libmysqlclient-dev uuid-dev libssl-dev"
	exit 2
fi

# One build per checkout, so worktrees and the bug loop never share (or fight over) one.
key="$(basename "$source_dir")-$(printf '%s' "$source_dir" | sha1sum | cut -c1-10)"
work="$HOME/.cache/mmo-gate/$key"
mkdir -p "$work/src"
exec 9>"$work/lock"
flock 9

step() {
	echo "== linux: $1 =="
}

step "sync $source_dir -> $work/src"
# Only what the server build and its tests read. data/ stays behind: the suites that look for
# game data skip without it, exactly as on a server build. .git stays behind too, so the
# version header does not change (and rebuild everything) with every commit.
rsync -a --delete --exclude=.git --exclude=__pycache__ \
	"$source_dir/CMakeLists.txt" "$source_dir/config.h.in" "$source_dir/version.h.in" \
	"$source_dir/cmake" "$source_dir/deps" "$source_dir/src" "$source_dir/deploy" \
	"$work/src/" || exit 1

step "configure"
# Every time: mmo_add_test globs its sources, and a new test file needs a configure to be seen.
cmake -S "$work/src" -B "$work/build" -G "Ninja Multi-Config" \
	-DCMAKE_C_COMPILER=gcc-13 -DCMAKE_CXX_COMPILER=g++-13 \
	-DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DMMO_WITH_DEV_COMMANDS=ON >"$work/configure.log" 2>&1 \
	|| { tail -n 60 "$work/configure.log"; exit 1; }

step "build ${targets[*]}"
cmake --build "$work/build" --config Debug -t "${targets[@]}" || exit 1

step "tests"
(cd "$work/build" && ctest -C Debug --output-on-failure) || exit 1

step "deployer tests"
(cd "$work/src" && python3 -m unittest discover -s deploy/deployer/tests -t deploy/deployer -p 'test_*.py') || exit 1

echo "linux gate green"
