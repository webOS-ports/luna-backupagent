#!/bin/sh
# A && pass || bad is safe here: pass and bad always succeed.
# shellcheck disable=SC2015
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# run-on-device.sh [iterations]
#
# Run on the device, as root, against the installed LunaBackupAgent. It
# changes nothing on the device: preBackup only lists files and postRestore
# only acknowledges. It checks the registration the backup service walks,
# that every file preBackup lists is a regular file and not a link, that
# postRestore takes the protocol's payload and refuses - but answers -
# anything else, and that the daemon neither restarts nor keeps allocating
# over two batches of calls (iterations each, default 500).
#
# Needs luna-send (in the image) and busybox; uses gdb when it is there.
set -u

N=${1:-500}
BA=luna://com.webos.service.backupagent
UNIT=luna-backupagent
fail=0
pass() { echo "PASS $*"; }
bad() { echo "FAIL $*"; fail=1; }

send() { luna-send -n 1 "$BA/$1" "$2" 2>&1; }
pid() { systemctl show -p MainPID --value "$UNIT"; }
rss() { awk '/VmRSS/ { print $2 }' "/proc/$(pid)/status"; }
restarts() { systemctl show -p NRestarts --value "$UNIT"; }

systemctl is-active -q "$UNIT" || { echo "$UNIT is not running"; exit 2; }
start_pid=$(pid)
start_restarts=$(restarts)

# ---- registration ------------------------------------------------------------
reg=/etc/palm/backup/com.webos.service.backupagent
if [ -f "$reg" ] && grep -q '"id" *: *"com.webos.service.backupagent"' "$reg"; then
	pass "registered in $reg"
else
	bad "$reg missing or names another service"
fi

# ---- preBackup -----------------------------------------------------------------
r=$(send preBackup '{"maxTempBytes":10485760,"tempDir":"/media/internal/.backup/"}')
case "$r" in
	*'"files"'*'['*) pass "preBackup lists files: $r" ;;
	*) bad "preBackup: $r" ;;
esac
case "$r" in *'"version"'*'"1.0"'*) pass "preBackup version" ;; *) bad "preBackup has no version: $r" ;; esac
case "$r" in *'"returnValue"'*false*) bad "preBackup failed: $r" ;; esac

# Every path listed: absolute, a regular file, not a symlink, listed once.
# The paths have no quotes or commas in them, so busybox tools will do.
# json-c writes "/" as "\/" (valid JSON, which the backup service decodes).
files=$(echo "$r" | sed -n 's/.*"files" *: *\[\([^]]*\)\].*/\1/p' | tr ',' '\n' \
	| sed 's/^ *"//; s/" *$//; s|\\/|/|g' | grep .)
for f in $files; do
	case "$f" in /*) ;; *) bad "not absolute: $f"; continue ;; esac
	if [ -L "$f" ]; then bad "listed a symlink: $f"
	elif [ -f "$f" ]; then pass "listed $f"
	else bad "listed something that is not a regular file: $f"
	fi
done
# shellcheck disable=SC2086  # one path per word, as listed above
dups=$(printf '%s\n' $files | sort | uniq -d)
[ -z "$dups" ] && pass "no file listed twice" || bad "listed twice: $dups"
case "$files" in *systemprefs.db*) bad "systemprefs.db belongs to systemservice's backup" ;; esac

# ---- postRestore ---------------------------------------------------------------
r=$(send postRestore '{"files":[],"tempDir":"/media/internal/.restore/"}')
case "$r" in *'"returnValue"'*true*) pass "postRestore accepts the protocol payload" ;; *) bad "postRestore: $r" ;; esac

# Hostile payloads: each must be answered, with returnValue false.
for p in '{}' '[]' 'null' '{"files":null}' '{"files":"x"}' '{"files":{}}' '{"Files":[]}'; do
	r=$(send postRestore "$p")
	case "$r" in
		*'"returnValue"'*false*'"errorText"'*|*'"errorText"'*'"returnValue"'*false*) pass "postRestore refuses $p" ;;
		*) bad "postRestore $p: $r" ;;
	esac
done
for p in '[]' 'null' '{"tempDir":42}'; do
	r=$(send preBackup "$p")
	case "$r" in *'"files"'*) pass "preBackup answers $p" ;; *) bad "preBackup $p: $r" ;; esac
done

# ---- no growth, no restart -----------------------------------------------------
# A warm-up batch first: glib and luna-service2 grow their buffers to fit the
# traffic once, and that is not a leak. Then two equal batches; a leak of even
# one 8-byte allocation per call adds at least 3 * N * 8 bytes to each, while
# what settles stays within a few hundred bytes. The second batch is the one
# judged, so a late bit of settling in the first does not count.
#
# With gdb on the device the measure is malloc's own count of bytes in use
# (malloc_stats(), which the daemon prints to the journal); without it, RSS,
# which only moves in pages and so needs the leak to fill a few.
pause() { sleep 1; }
measure() {
	if command -v gdb >/dev/null 2>&1; then
		since=$(date +%s); pause
		gdb -p "$(pid)" -batch -ex 'call (void) malloc_stats()' >/dev/null 2>&1
		pause
		journalctl -u "$UNIT" --since "@$since" --no-pager | grep 'in use bytes' | tail -n 1 | awk '{ print $NF }'
	else
		echo $(( $(rss) * 1024 ))
	fi
}
rounds() {
	i=0
	while [ $i -lt "$1" ]; do
		send preBackup '{}' >/dev/null
		send postRestore '{"files":[]}' >/dev/null
		send postRestore '[]' >/dev/null
		i=$((i + 1))
	done
}
command -v gdb >/dev/null 2>&1 && what="malloc in-use bytes" || what="RSS bytes"
rounds $((N / 5 + 1))
m0=$(measure)
rounds "$N"
m1=$(measure)
rounds "$N"
m2=$(measure)
limit=$((3 * N * 8))
case "$m0$m1$m2" in
	''|*[!0-9]*) bad "could not measure memory ($m0 / $m1 / $m2)" ;;
	*)
		if [ $((m2 - m1)) -lt "$limit" ]; then
			pass "$what after warm-up $m0, +$((3 * N)) calls $m1, +$((6 * N)) calls $m2 (a per-call leak adds >= $limit per batch)"
		else
			bad "$what after warm-up $m0, +$((3 * N)) calls $m1, +$((6 * N)) calls $m2: grows by >= $limit per batch"
		fi ;;
esac

[ "$(pid)" = "$start_pid" ] && [ "$(restarts)" = "$start_restarts" ] \
	&& pass "the daemon did not restart" || bad "the daemon restarted"

exit $fail
