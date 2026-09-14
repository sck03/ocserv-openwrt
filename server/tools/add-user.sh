#!/bin/sh
# Interactive password input; stores only the ocpasswd hash in the UCI account entry.
set -eu
account=${1:?Usage: add-user.sh employee01}
case "$account" in ''|*[!a-zA-Z0-9_.-]*) printf '%s\n' 'Use letters, digits, dot, underscore or hyphen in the account name.' >&2; exit 1;; esac
[ "${#account}" -le 64 ] || exit 1
command -v ocpasswd >/dev/null
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
umask 077
scratch=$(mktemp /tmp/bulijie-ocpasswd.XXXXXX)
trap 'rm -f "$scratch"' EXIT HUP INT TERM
ocpasswd -c "$scratch" "$account"
IFS=: read -r generated_name generated_group password_hash < "$scratch"
[ "$generated_name" = "$account" ] && [ -n "$password_hash" ] || exit 1
if [ -z "$found" ]; then found=$(uci add ocserv ocservusers); fi
uci set "ocserv.$found.name=$account"
uci set "ocserv.$found.group=*"
uci set "ocserv.$found.password=$password_hash"
uci commit ocserv
printf '%s\n' 'Account saved. Restart ocserv to load the account; existing sessions may disconnect.'
