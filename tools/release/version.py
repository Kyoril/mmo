#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Game and launcher version numbers, derived from git tags.

The game version is MAJOR.MINOR.PATCH from the newest reachable `vX.Y.Z` tag, plus the commit
count as the fourth component. The launcher version is MAJOR.MINOR.PATCH from the newest
reachable `launcher-vX.Y.Z` tag. cmake/mmo_version.cmake applies the same rules to the build.
"""

import argparse
import re
import subprocess
import sys

GAME_PREFIX = "v"
LAUNCHER_PREFIX = "launcher-v"


def parse_tag(tag, prefix):
	"""(major, minor, patch) for `<prefix>X.Y.Z`, or None."""
	match = re.fullmatch(re.escape(prefix) + r"(\d+)\.(\d+)\.(\d+)", tag.strip())
	if not match:
		return None
	return tuple(int(part) for part in match.groups())


def _git(repo, *args):
	result = subprocess.run(["git", "-C", repo] + list(args), capture_output=True, text=True)
	return result.stdout.strip() if result.returncode == 0 else ""


def describe(repo, prefix, commit="HEAD"):
	"""(major, minor, patch) of the newest `<prefix>X.Y.Z` tag reachable from `commit`; zeros if none."""
	tag = _git(repo, "describe", "--tags", "--abbrev=0", "--match", prefix + "[0-9]*", commit)
	return parse_tag(tag, prefix) or (0, 0, 0)


def commit_count(repo, commit="HEAD"):
	count = _git(repo, "rev-list", "--count", commit)
	return int(count) if count.isdigit() else 0


def game_version(repo, commit="HEAD"):
	major, minor, patch = describe(repo, GAME_PREFIX, commit)
	return "{}.{}.{}.{}".format(major, minor, patch, commit_count(repo, commit))


def launcher_version(repo, commit="HEAD"):
	return "{}.{}.{}".format(*describe(repo, LAUNCHER_PREFIX, commit))


def main(argv=None):
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", default=".")
	parser.add_argument("--commit", default="HEAD")
	parser.add_argument("--launcher", action="store_true", help="print the launcher version instead")
	args = parser.parse_args(argv)
	print(launcher_version(args.repo, args.commit) if args.launcher else game_version(args.repo, args.commit))
	return 0


if __name__ == "__main__":
	sys.exit(main())
