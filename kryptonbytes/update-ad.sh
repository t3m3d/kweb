#!/usr/bin/env bash
# One-command sponsor-ad update: trim border -> deploy -> verify.
# Usage: ./update-ad.sh <new-raw-image.png> [inset]
#   e.g. ./update-ad.sh ../../thepeoplewire/ads/vertical/third.png
set -euo pipefail
cd "$(dirname "$0")"

SRC="${1:?usage: ./update-ad.sh <raw-image.png> [inset]}"
INSET="${2:-9}"
OUT="dist/ads/second.png"
REMOTE="ftp://88.223.85.17/ads/second.png"

# FTP creds live in .deploy.env (gitignored): FTP_USER, FTP_PASS
[ -f .deploy.env ] || { echo "missing .deploy.env (FTP_USER/FTP_PASS)"; exit 1; }
# shellcheck disable=SC1091
source .deploy.env

echo "==> trimming $SRC"
.venv/bin/python tools/trim_ad.py "$SRC" "$OUT" "$INSET"

echo "==> uploading to kryptonbytes.com"
curl --user "$FTP_USER:$FTP_PASS" --connect-timeout 25 -s -T "$OUT" "$REMOTE"

echo "==> verifying live"
code=$(curl -s -o /dev/null -w '%{http_code}' "https://kryptonbytes.com/ads/second.png?v=$(date +%s)")
echo "    https://kryptonbytes.com/ads/second.png -> HTTP $code  ($(wc -c < "$OUT") bytes)"
[ "$code" = "200" ] && echo "==> done. hard-refresh (Cmd+Shift+R) to see it." || { echo "!! live check failed"; exit 1; }
