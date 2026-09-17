# Native Windows client references

The interface and lifecycle follow [OpenConnect GUI v1.6.2](https://gitlab.com/openconnect/openconnect-gui/-/tree/v1.6.2), tag commit c89e5dc00a2b24e5b1c84855b91bcdda3aee4d86. Its GPL-2.0-or-later attribution is retained in adapted files. The UI uses Win32/C++17; Qt is neither linked nor distributed.

The port includes Main / VPN Info tabs, quick and advanced profiles, text/password/choice forms, certificate confirmation, software tokens, system certificates, logs, tray controls, settings and help. Chinese is the initial language; English is available in Settings. Protocol capability comes from OpenConnect. PKCS#11, TPM and browser SSO are not enabled.

## Vendored sources

- vendor/json.hpp: [nlohmann/json 3.12.0](https://github.com/nlohmann/json/tree/v3.12.0), MIT; complete single-header source and license included.
- vendor/vpnc-script-win.js: [vpnc-scripts commit ce9e961bd0f6b867e1c7c35f78f6fb973f6ff101](https://gitlab.com/openconnect/vpnc-scripts/-/tree/ce9e961bd0f6b867e1c7c35f78f6fb973f6ff101). Local changes select a per-session UTF-16 log, wait for command completion, keep failures sticky, tolerate empty DNS/WINS cleanup and unavailable IPv6 discovery, honor log level zero, and skip unnecessary loopback gateway routes.
- GUI and vpnc-script licenses accompany source and binary packages.

The Windows helper explicitly catches JScript exceptions and exits nonzero: WSH itself can report an unhandled runtime error yet return status 0. Native tests execute the shipped helper with an invalid log path to verify this failure boundary.

Library source URLs, versions and SHA-256 values live in scripts/sources.json. Extraction is unmodified; patches apply with zero fuzz to a build-local copy.

## OpenConnect patch

patches/openconnect/0001-windows-script-errors.patch fixes the official 9.21 core:

1. Check pre-init/connect results; tear down a failed tunnel and reset its native handles so it is no longer considered up.
2. Stop when an application-provided tunnel callback fails instead of silently retrying setup.
3. Propagate attempt-reconnect/reconnect failures before reporting reconnection.
4. Use the system Windows Script Host and assign its process tree to a job before execution. Stop it on timeout. Nonzero exit codes, including 259, remain failures.

patches/stoken/0001-static-linking.patch adds a STOKEN_STATIC switch to stoken 0.92's public header. Both the library and OpenConnect use it to avoid DLL imports in a static build. The recipe only builds/installs libstoken and its headers/pkg-config file, not the unused CLI or GUI.

patches/gnutls/0001-static-linking.patch makes GNUTLS_STATIC suppress DLL imports for GnuTLS's allocator variables. This keeps both the client and OpenConnect linked to the actual static library.

Script regressions execute the shipped JScript with mocked Windows commands. Authentication regressions execute real OpenConnect/GnuTLS callbacks against loopback HTTPS fixtures. UI tests operate only their own windows. The isolated x64 Windows CI runner additionally checks real Wintun/CSTP UDP traffic, script failure and adapter cleanup. See [validation boundaries](../docs/VALIDATION.md) for hardware acceptance.

## Source layout and migration

client/ is the sole client implementation. The old src/ network/WFP layer, Credential Manager store, default INI and OpenSSL recipe are retired. Profiles use application-local JSON and current-user DPAPI. Legacy public .bvpn files can be imported; credentials and settings outside this repository are not automatically migrated or deleted.
