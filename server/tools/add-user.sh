#!/bin/sh
# Interactive password input; stores only the ocpasswd hash in the UCI account entry.
set -eu
umask 077
[ "$(id -u)" = 0 ] || { printf '%s\n' 'Run this script as root.' >&2; exit 1; }
account=${1:?Usage: add-user.sh employee01}
case "$account" in ''|-*|*[!a-zA-Z0-9_.-]*) printf '%s\n' 'Use letters, digits, dot, underscore or hyphen; do not start the account name with a hyphen.' >&2; exit 1;; esac
[ "${#account}" -le 64 ] || exit 1
command -v ocpasswd >/dev/null
directory=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
delta=''
cleanup() {
    rm -f "$scratch"
    if [ -n "$delta" ]; then rm -f "$delta/ocserv"; rmdir "$delta"; fi
}
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
if [ "${OCSERV_EASY_LOCK_OWNER:-}" != "$$" ]; then
    [ -f "$directory/with-lock.lua" ] && [ -f "$directory/lease.lua" ] || { printf '%s\n' 'Extract the complete server bundle before running add-user.sh.' >&2; exit 1; }
    command -v lua >/dev/null
    # Interactive input must not hold up LuCI or the recovery worker. Only the
    # resulting root-only hash file crosses into the locked maintenance phase.
    scratch=$(mktemp /tmp/lansyra-ocpasswd.XXXXXX)
    trap cleanup EXIT
    ocpasswd -c "$scratch" "$account"
    lua "$directory/with-lock.lua" /bin/sh "$directory/add-user.sh" "$account" "$scratch"
    exit
fi
scratch=${2:?Missing password hash file}
trap cleanup EXIT
[ ! -f /etc/ocserv/easy-transaction/state.json ] || { printf '%s\n' 'A management transaction needs recovery; resolve it in LuCI first.' >&2; exit 1; }
[ -z "$(uci -q changes ocserv)" ] || { printf '%s\n' 'Unapplied ocserv edits exist; resolve them in LuCI first.' >&2; exit 1; }
. /lib/functions.sh
found=''
matches=0
find_account() {
    local name
    config_get name "$1" name
    if [ "$name" = "$account" ]; then found=$1; matches=$((matches + 1)); fi
}
config_load ocserv
config_foreach find_account ocservusers
[ "$matches" -le 1 ] || { printf '%s\n' 'Duplicate account entries: resolve them in LuCI first.' >&2; exit 1; }
IFS=: read -r generated_name generated_group password_hash < "$scratch"
[ "$generated_name" = "$account" ] && [ -n "$password_hash" ] || exit 1
delta=$(mktemp -d /tmp/lansyra-user-uci.XXXXXX)
if [ -z "$found" ]; then found=$(uci -t "$delta" add ocserv ocservusers); fi
uci -t "$delta" set "ocserv.$found.name=$account"
uci -t "$delta" set "ocserv.$found.group=*"
uci -t "$delta" set "ocserv.$found.password=$password_hash"
uci -t "$delta" commit ocserv
printf '%s\n' 'Account saved. Restart ocserv to load the account; existing sessions may disconnect.'
