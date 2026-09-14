#!/bin/sh
# Upgrade one OpenWrt 24.10 userspace package. Does not change feeds or kernel modules.
# Usage: sh upgrade-ocserv-24.10.sh --check NEW.ipk OLD.ipk
#        sh upgrade-ocserv-24.10.sh --apply NEW.ipk OLD.ipk
set -eu
umask 077
mode=${1:-}
new=${2:-}
old=${3:-}
case "$mode" in --check|--apply) ;; *) printf '%s\n' 'Usage: upgrade-ocserv-24.10.sh --check|--apply NEW.ipk OLD.ipk' >&2; exit 1;; esac
[ "$(id -u)" = 0 ] || { printf '%s\n' 'Run on the router as root.' >&2; exit 1; }
command -v opkg >/dev/null 2>&1 || { printf '%s\n' 'This script is for IPK/opkg firmware. For 25.12 use the matching APK build and migration guide.' >&2; exit 1; }
[ -c /dev/net/tun ] || { printf '%s\n' 'The current firmware must provide /dev/net/tun first.' >&2; exit 1; }
opkg print-architecture | grep -q '^arch aarch64_generic ' || { printf '%s\n' 'This bundle targets aarch64_generic.' >&2; exit 1; }
case "$(. /etc/openwrt_release; printf '%s' "$DISTRIB_RELEASE")" in 24.10*) ;; *) printf '%s\n' 'Use a package built for this firmware release.' >&2; exit 1;; esac
[ -f "$new" ] && [ -f "$old" ] || { printf '%s\n' 'Provide both the new package and the exact old package for rollback.' >&2; exit 1; }
new=$(readlink -f "$new")
old=$(readlink -f "$old")
installed=$(opkg status ocserv | sed -n 's/^Version: //p')
[ -n "$installed" ] || { printf '%s\n' 'ocserv is not installed. This script upgrades an existing installation.' >&2; exit 1; }
stage=$(mktemp -d /tmp/ocserv-upgrade.XXXXXX)
changed=0
backup=''
was_running=0
was_enabled=0
/etc/init.d/ocserv running >/dev/null 2>&1 && was_running=1
/etc/init.d/ocserv enabled >/dev/null 2>&1 && was_enabled=1

restore_service_state() {
    if [ "$was_enabled" = 1 ]; then /etc/init.d/ocserv enable; else /etc/init.d/ocserv disable; fi
    if [ "$was_running" = 1 ]; then /etc/init.d/ocserv start; else /etc/init.d/ocserv stop; fi
}
finish() {
    code=$?
    trap - EXIT HUP INT TERM
    if [ "$changed" = 1 ]; then
        printf '%s\n' 'Upgrade did not pass validation. Restoring the previous package and configuration.' >&2
        rollback_ok=1
        /etc/init.d/ocserv stop >/dev/null 2>&1 || true
        opkg install --force-downgrade "$backup/previous.ipk" > "$backup/rollback.log" 2>&1 || rollback_ok=0
        tar -xzf "$backup/config.tar.gz" -C / || rollback_ok=0
        restore_service_state || rollback_ok=0
        if [ "$rollback_ok" = 1 ]; then
            printf 'Rollback completed. Backup: %s\n' "$backup" >&2
        else
            printf 'ROLLBACK INCOMPLETE. Check %s/rollback.log and restore %s/config.tar.gz.\n' "$backup" "$backup" >&2
        fi
        code=1
    fi
    # mktemp created this exact private directory; refuse an unexpected cleanup target.
    case "$stage" in /tmp/ocserv-upgrade.?*) rm -rf -- "$stage";; esac
    exit "$code"
}
trap finish EXIT
trap 'exit 1' HUP INT TERM

verify_checksum() {
    package=$1
    folder=$(dirname "$package")
    name=$(basename "$package")
    case "$name" in *[!A-Za-z0-9_.+-]*) printf '%s\n' 'Unsupported package filename.' >&2; exit 1;; esac
    [ -f "$folder/SHA256SUMS" ] || { printf 'Missing %s/SHA256SUMS\n' "$folder" >&2; exit 1; }
    line=$(awk -v name="$name" '$2 == name || $2 == "*" name {print}' "$folder/SHA256SUMS")
    [ "$(printf '%s\n' "$line" | wc -l)" = 1 ] && [ -n "$line" ] || { printf 'Missing or duplicate checksum for %s\n' "$name" >&2; exit 1; }
    (cd "$folder" && printf '%s\n' "$line" | sha256sum -c -)
}
read_control() {
    package=$1
    destination=$2
    # OpenWrt SDK ipkg-build uses a gzip/tar outer container. Fail closed for another format.
    tar -xzOf "$package" ./control.tar.gz > "$stage/control.tar.gz"
    tar -xzOf "$stage/control.tar.gz" ./control > "$destination"
    grep -qx 'Package: ocserv' "$destination" || { printf '%s\n' 'The package is not ocserv.' >&2; exit 1; }
    grep -qx 'Architecture: aarch64_generic' "$destination" || { printf '%s\n' 'The package architecture does not match.' >&2; exit 1; }
}
verify_checksum "$new"
verify_checksum "$old"
read_control "$new" "$stage/new.control"
read_control "$old" "$stage/old.control"
new_version=$(sed -n 's/^Version: //p' "$stage/new.control")
old_version=$(sed -n 's/^Version: //p' "$stage/old.control")
[ "$old_version" = "$installed" ] || { printf 'Rollback version %s must match installed version %s.\n' "$old_version" "$installed" >&2; exit 1; }
opkg compare-versions "$new_version" '>=' '1.5.0' || {
    printf '%s\n' 'The management page requires ocserv 1.5.0 or newer.' >&2; exit 1
}
opkg compare-versions "$new_version" '>>' "$installed" || {
    printf '%s\n' 'The new package must be newer than the installed version.' >&2; exit 1
}
opkg --noaction install "$new" > "$stage/plan.log" 2>&1 || { cat "$stage/plan.log" >&2; exit 1; }
if grep -Eq '([Ii]nstalling|[Uu]pgrading|[Dd]owngrading).*kmod-' "$stage/plan.log"; then
    printf '%s\n' 'Refusing an operation that would install or replace a kernel module.' >&2
    exit 1
fi
tar -xzOf "$new" ./data.tar.gz > "$stage/data.tar.gz"
tar -xzOf "$stage/data.tar.gz" ./usr/sbin/ocserv > "$stage/ocserv"
chmod 700 "$stage/ocserv"
"$stage/ocserv" --version > "$stage/version.log" 2>&1 || {
    printf '%s\n' 'The new executable does not run with the installed userspace libraries. No package was changed.' >&2
    cat "$stage/version.log" >&2
    exit 1
}
if [ -f /var/etc/ocserv.conf ]; then
    "$stage/ocserv" --test-config --config /var/etc/ocserv.conf > "$stage/config-check.log" 2>&1 || {
        printf '%s\n' 'The existing runtime configuration was rejected. No package was changed.' >&2
        exit 1
    }
fi
printf 'Checks passed: ocserv %s -> %s; current TUN and kernel modules retained.\n' "$installed" "$new_version"
if [ "$mode" = --check ]; then
    printf '%s\n' 'No package or configuration was changed. Run with --apply to upgrade; active VPN connections will disconnect briefly.'
    exit 0
fi

backup="/root/ocserv-upgrade-$(date +%Y%m%d-%H%M%S)"
mkdir -m 700 "$backup"
cp "$old" "$backup/previous.ipk"
tar -czf "$backup/config.tar.gz" -C / etc/config/ocserv etc/ocserv etc/init.d/ocserv
printf 'PreviousVersion=%s\nNewVersion=%s\nWasRunning=%s\nWasEnabled=%s\n' "$installed" "$new_version" "$was_running" "$was_enabled" > "$backup/upgrade-state.txt"
changed=1
/etc/init.d/ocserv stop
opkg install "$new" </dev/null > "$backup/install.log" 2>&1 || { printf 'Package installation failed; see %s/install.log\n' "$backup" >&2; exit 1; }
/etc/init.d/ocserv stop >/dev/null 2>&1 || true
/usr/sbin/ocserv --version > "$backup/new-version.txt" 2>&1
/usr/bin/occtl help > "$backup/occtl-help.txt" 2>&1 || true
grep -q 'terminate user' "$backup/occtl-help.txt" || { printf '%s\n' 'The required occtl session-revocation command is missing.' >&2; exit 1; }
restore_service_state
if [ "$was_running" = 1 ]; then
    ready=0
    for try in 1 2 3 4 5 6 7 8 9 10; do
        if /etc/init.d/ocserv running >/dev/null 2>&1 && /usr/bin/occtl show status >/dev/null 2>&1; then ready=1; break; fi
        sleep 1
    done
    [ "$ready" = 1 ] || { printf '%s\n' 'The new service did not become ready.' >&2; exit 1; }
    /usr/sbin/ocserv --test-config --config /var/etc/ocserv.conf > "$backup/config-check.txt" 2>&1
fi
changed=0
printf 'Upgrade completed. ocserv %s. Private backup: %s\n' "$new_version" "$backup"
printf '%s\n' 'Install luci-app-ocserv-easy next, then reopen LuCI. Validate a real VPN login, DNS and traffic forwarding.'
