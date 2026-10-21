#!/usr/bin/env bash
# KettleLAN – GitHub release description: the CHANGELOG.md entry + download table + verification details.
#   scripts/release-notes.sh 0.1.0 [commit] > notes.md
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION="${1:?Give a version, e.g. 0.1.0}"
COMMIT="${2:-$(git rev-parse "v$VERSION^{commit}" 2>/dev/null || git rev-parse HEAD)}"
SDK_SHA="$(cut -d' ' -f1 cmake/steamworks-sdk.sha256)"
SDK_FILE="$(awk '{print $2}' cmake/steamworks-sdk.sha256)"
REPO="${GITHUB_REPOSITORY:-maxiuz94/kettlelan}"

# the "## [VERSION] …" section up to the next "## ["
BODY="$(awk -v v="$VERSION" '
    /^## \[/ { if (found) exit; if (index($0, "## [" v "]") == 1) { found = 1; next } }
    found' CHANGELOG.md)"
if [ -z "$(printf '%s' "$BODY" | tr -d '[:space:]')" ]; then
    echo "No entry for version $VERSION in CHANGELOG.md" >&2
    exit 1
fi

printf '%s\n' "$BODY" | sed -e '/./,$!d'   # drop leading empty lines
cat << NOTES

### Download
| System | File |
|--------|------|
| Any Linux distribution | \`KettleLAN-${VERSION}-x86_64.AppImage\` |
| Ubuntu 24.04+, Mint 22+, Pop!_OS, Debian 13 | \`kettlelan_${VERSION}_ubuntu24.04_amd64.deb\` |
| Debian 12, MX Linux 23 | \`kettlelan_${VERSION}_debian12_amd64.deb\` |

AppImage: \`chmod +x KettleLAN-*.AppImage\` and run it. Debian package: \`sudo apt install ./file.deb\`.
Steam must be installed and signed in. 🇵🇱 [Instrukcja po polsku](https://github.com/${REPO}/blob/main/README.pl.md)

<details>
<summary>Verifying the files and where they come from</summary>

- Built by GitHub Actions from commit \`${COMMIT}\` – the build log is public in the Actions tab.
- Check the origin of a file: \`gh attestation verify FILE -R ${REPO}\`
- The packages include the Steam library (\`libsteam_api.so\`), which Valve allows to be shipped with
  applications. They were built with the official Steamworks SDK (\`${SDK_FILE}\`, SHA256 \`${SDK_SHA}\`)
  from https://partner.steamgames.com/downloads/list – the SDK is not part of the repository because
  Valve's license does not allow it.
</details>
NOTES
