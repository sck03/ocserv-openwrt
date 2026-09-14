#!/bin/sh
# Exports public trust information only. Run on the server after configuring a valid server certificate.
set -eu
server=${1:-https://192.168.19.254:4443}
ca=${2:-/etc/ocserv/ca.pem}
output=${3:-/tmp/company-vpn.bvpn}
case "$server" in https://*) ;; *) printf '%s\n' 'Use an HTTPS server URL.' >&2; exit 1;; esac
case "$server" in *[!a-zA-Z0-9:/._\[\]-]*) printf '%s\n' 'Use an ASCII hostname or IP URL without query parameters.' >&2; exit 1;; esac
grep -q 'BEGIN CERTIFICATE' "$ca" || { printf '%s\n' 'A PEM CA certificate is required.' >&2; exit 1; }
if grep -q 'PRIVATE KEY' "$ca"; then printf '%s\n' 'Refusing to export a private key.' >&2; exit 1; fi
umask 077
{
    printf '[VPN]\nServer=%s\nCABase64=' "$server"
    base64 "$ca" | tr -d '\r\n'
    printf '\n'
} > "$output"
printf 'Created %s. Distribute this public profile through your normal trusted company channel.\n' "$output"
