#!/usr/bin/env bash
# KettleLAN – GitHub release description: the CHANGELOG.md entry for the given version.
#   scripts/release-notes.sh 0.1.0 > notes.md
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION="${1:?Give a version, e.g. 0.1.0}"

# the "## [VERSION] …" section up to the next "## ["
BODY="$(awk -v v="$VERSION" '
    /^## \[/ { if (found) exit; if (index($0, "## [" v "]") == 1) { found = 1; next } }
    found' CHANGELOG.md)"
if [ -z "$(printf '%s' "$BODY" | tr -d '[:space:]')" ]; then
    echo "No entry for version $VERSION in CHANGELOG.md" >&2
    exit 1
fi

printf '%s\n' "$BODY" | sed -e '/./,$!d'   # drop leading empty lines
