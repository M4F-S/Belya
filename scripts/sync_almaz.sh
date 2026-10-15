#!/usr/bin/env bash
# H21 (2026-10-15): DISABLED. The two repositories are independent forks, not twins, and no
# file is guaranteed to be safe to copy between them. This script used to overwrite "shared
# pure-infrastructure" files from one repo into the other, which is exactly the blind-copy
# this project forbids: a change must be applied per repo and verified against that repo's
# own suite. Kept only so the history and the intent stay visible; it refuses to run.
echo "[-] sync_almaz.sh is disabled: the repos are independent forks (see README, H21)." >&2
echo "[-] Apply and test the change in each repo instead." >&2
exit 1
