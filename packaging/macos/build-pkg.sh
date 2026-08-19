#!/usr/bin/env bash
#
# Builds NITEDRIVE.pkg from bundles that are already built and signed.
#
#   ./packaging/macos/build-pkg.sh <dist-dir> <output-dir> [version]
#
# <dist-dir> holds NITEDRIVE.vst3 and/or NITEDRIVE.component. Whichever are
# present get packaged; the installer offers the user a choice between them.
#
# Signing the package needs a "Developer ID Installer" certificate, which is a
# DIFFERENT certificate from the "Developer ID Application" one used on the
# bundles themselves. Set MACOS_INSTALLER_IDENTITY to use it. Without it an
# unsigned package is produced, which still installs but makes Gatekeeper
# complain on first open.
set -euo pipefail

DIST="${1:?usage: build-pkg.sh <dist-dir> <output-dir> [version]}"
OUT="${2:?usage: build-pkg.sh <dist-dir> <output-dir> [version]}"
VERSION="${3:-1.0.0}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$OUT"

have_vst3=0
have_au=0
[ -d "$DIST/NITEDRIVE.vst3" ]      && have_vst3=1
[ -d "$DIST/NITEDRIVE.component" ] && have_au=1

if [ "$have_vst3" -eq 0 ] && [ "$have_au" -eq 0 ]; then
    echo "error: no NITEDRIVE.vst3 or NITEDRIVE.component in $DIST" >&2
    exit 1
fi

# pkgbuild copies the whole --root, so each format needs its own staging tree
# containing exactly what belongs at that install location.
if [ "$have_vst3" -eq 1 ]; then
    mkdir -p "$WORK/stage-vst3"
    cp -R "$DIST/NITEDRIVE.vst3" "$WORK/stage-vst3/"
    pkgbuild --identifier com.nitedriveaudio.nitedrive.vst3 \
             --version "$VERSION" \
             --install-location /Library/Audio/Plug-Ins/VST3 \
             --root "$WORK/stage-vst3" \
             --scripts "$HERE/scripts" \
             "$WORK/NITEDRIVE-VST3.pkg"
    echo "built component package: VST3"
fi

if [ "$have_au" -eq 1 ]; then
    mkdir -p "$WORK/stage-au"
    cp -R "$DIST/NITEDRIVE.component" "$WORK/stage-au/"
    pkgbuild --identifier com.nitedriveaudio.nitedrive.au \
             --version "$VERSION" \
             --install-location /Library/Audio/Plug-Ins/Components \
             --root "$WORK/stage-au" \
             --scripts "$HERE/scripts" \
             "$WORK/NITEDRIVE-AU.pkg"
    echo "built component package: Audio Unit"
fi

# Drop choices for formats that were not built, so the installer never offers to
# install something that is not in the package.
DIST_XML="$WORK/distribution.xml"
cp "$HERE/distribution.xml" "$DIST_XML"

strip_choice() {
    python3 - "$DIST_XML" "$1" "$2" <<'PY'
import re, sys
path, choice, ident = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(path).read()
s = re.sub(r'\n\s*<line choice="%s"/>' % re.escape(choice), '', s)
s = re.sub(r'\n\s*<choice id="%s".*?</choice>' % re.escape(choice), '', s, flags=re.S)
s = re.sub(r'\n\s*<pkg-ref id="%s".*?</pkg-ref>' % re.escape(ident), '', s)
open(path, 'w').write(s)
PY
}

[ "$have_vst3" -eq 0 ] && strip_choice "choice.vst3" "com.nitedriveaudio.nitedrive.vst3"
[ "$have_au"   -eq 0 ] && strip_choice "choice.au"   "com.nitedriveaudio.nitedrive.au"

sed -i '' "s/version=\"1\.0\.0\"/version=\"$VERSION\"/g" "$DIST_XML" 2>/dev/null \
    || sed -i "s/version=\"1\.0\.0\"/version=\"$VERSION\"/g" "$DIST_XML"

UNSIGNED="$WORK/NITEDRIVE-unsigned.pkg"

productbuild --distribution "$DIST_XML" \
             --package-path "$WORK" \
             --resources "$HERE/resources" \
             "$UNSIGNED"

FINAL="$OUT/NITEDRIVE-$VERSION.pkg"

if [ -n "${MACOS_INSTALLER_IDENTITY:-}" ]; then
    productsign --sign "$MACOS_INSTALLER_IDENTITY" "$UNSIGNED" "$FINAL"
    pkgutil --check-signature "$FINAL"
    echo "signed installer: $FINAL"
else
    cp "$UNSIGNED" "$FINAL"
    echo "::warning::No MACOS_INSTALLER_IDENTITY - the package is unsigned. It installs, but Gatekeeper will warn on first open."
    echo "unsigned installer: $FINAL"
fi

echo "$FINAL"
