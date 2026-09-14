#!/usr/bin/env bash
# Publish complete, verified build assets; keep failed uploads in an unpublished draft.
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 client|server ASSET_DIRECTORY" >&2
  exit 2
fi
component="$1"
asset_directory="$2"
case "$component" in
  client) title="布利杰VPN Windows 客户端" ;;
  server) title="ocserv OpenWrt 服务端与中文管理页" ;;
  *) echo "Unknown release component: $component" >&2; exit 2 ;;
esac

: "${GH_TOKEN:?The release job requires GITHUB_TOKEN with contents: write}"
: "${GITHUB_REPOSITORY:?}"
: "${GITHUB_SHA:?}"
: "${GITHUB_RUN_ID:?}"
: "${GITHUB_RUN_ATTEMPT:?}"
tag="${component}-${GITHUB_RUN_ID}-${GITHUB_RUN_ATTEMPT}"
server_url="${GITHUB_SERVER_URL:-https://github.com}"
run_url="$server_url/$GITHUB_REPOSITORY/actions/runs/$GITHUB_RUN_ID/attempts/$GITHUB_RUN_ATTEMPT"
release_url="$server_url/$GITHUB_REPOSITORY/releases/tag/$tag"

shopt -s nullglob
archives=("$asset_directory"/*.zip)
if [[ ${#archives[@]} -eq 0 || ! -s "$asset_directory/SHA256SUMS.txt" ]]; then
  echo "Release requires ZIP packages and SHA256SUMS.txt" >&2
  exit 1
fi
actual_checksums="$(cd "$asset_directory" && sha256sum --binary -- *.zip | sed 's/ \*/  /' | LC_ALL=C sort)"
expected_checksums="$(LC_ALL=C sort "$asset_directory/SHA256SUMS.txt")"
if [[ "$actual_checksums" != "$expected_checksums" ]]; then
  echo "ZIP files do not match SHA256SUMS.txt" >&2
  exit 1
fi

notes="$(mktemp)"
trap 'rm -f -- "$notes"' EXIT
{
  printf '%s\n\n' "$title，由手动构建自动发布。"
  printf '%s\n\n' '下载对应 ZIP 后完整解压，使用 SHA256SUMS.txt 核验下载文件。'
  if [[ "$component" == client ]]; then
    printf '%s\n\n' '包含 Windows x64/x86 便携客户端和对应源码包（含第三方源码）。组件版本见包内 BUILDINFO.json。'
  else
    printf '%s\n\n' '每个 SDK 套装包含 ocserv、原 LuCI 页面、中文翻译、布利杰管理页，以及对应源码、安装说明和逐文件校验值。具体 SDK 版本见文件名及 BUILDINFO.json。'
  fi
  printf '%s\n' '本次附件：'
  for archive in "${archives[@]}"; do
    printf -- '- %s\n' "${archive##*/}"
  done
  printf '\n源码提交：%s\n\n[构建记录](%s)\n\n' "$GITHUB_SHA" "$run_url"
  printf '%s\n' '当前按联调预发布版提供；编译和静态检查不代替 Win7、N1/OPL 实机与完整 VPN 转发验收。'
} > "$notes"

# Run ID and attempt isolate independent builds and reruns from existing releases.
# Never replace published assets; a failed upload leaves this attempt as a draft.
gh release create "$tag" --repo "$GITHUB_REPOSITORY" --target "$GITHUB_SHA" \
  --title "$title（构建 ${GITHUB_RUN_NUMBER:-$GITHUB_RUN_ID}.$GITHUB_RUN_ATTEMPT）" \
  --notes-file "$notes" --draft --prerelease --latest=false
gh release upload "$tag" "${archives[@]}" "$asset_directory/SHA256SUMS.txt" --repo "$GITHUB_REPOSITORY"
gh release edit "$tag" --repo "$GITHUB_REPOSITORY" --draft=false --latest=false

printf 'Published %s\n' "$release_url"
if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
  printf '### %s\n\n[下载 Release](%s)\n' "$title" "$release_url" >> "$GITHUB_STEP_SUMMARY"
fi
