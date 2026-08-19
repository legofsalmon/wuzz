#!/usr/bin/env bash
#
# Collects the values needed for the macOS signing and notarisation secrets.
#
# Run it on your Mac. Nothing is uploaded and nothing is written to disk; secret
# material goes to the clipboard rather than the terminal, so it does not end up in
# your shell history or scrollback.
#
#   ./tools/signing-secrets.sh identity          list Developer ID certificates
#   ./tools/signing-secrets.sh cert  <file.p12>  copy MACOS_CERTIFICATE
#   ./tools/signing-secrets.sh notary <file.p8>  copy NOTARY_KEY, show NOTARY_KEY_ID
#
set -euo pipefail

die() { echo "error: $*" >&2; exit 1; }

copy() {
    if command -v pbcopy >/dev/null 2>&1; then
        pbcopy
        echo "  -> copied to clipboard"
    else
        cat
        echo
        echo "  (no pbcopy found, printed above)"
    fi
}

case "${1:-}" in
identity)
    echo "Developer ID certificates in your keychain:"
    echo
    # Only Developer ID Application is valid for software shipped outside the App
    # Store; Apple Development and Apple Distribution are rejected at notarisation.
    if ! security find-identity -v -p codesigning | grep "Developer ID Application"; then
        echo "  none found."
        echo
        echo "  Create one in Xcode: Settings -> Accounts -> your Apple ID ->"
        echo "  Manage Certificates -> + -> Developer ID Application."
        exit 1
    fi
    echo
    echo "MACOS_SIGNING_IDENTITY is the quoted part, without the quotes, e.g."
    echo "  Developer ID Application: Your Name (ABCDE12345)"
    echo
    echo "The 10 characters in the parentheses are also your APPLE_TEAM_ID."
    ;;

cert)
    P12="${2:-}"
    [ -n "$P12" ] || die "usage: $0 cert <file.p12>"
    [ -f "$P12" ] || die "no such file: $P12"

    echo "MACOS_CERTIFICATE:"
    base64 -i "$P12" | copy
    echo
    echo "MACOS_CERTIFICATE_PWD is the password you set when exporting the .p12."
    ;;

notary)
    P8="${2:-}"
    [ -n "$P8" ] || die "usage: $0 notary <AuthKey_XXXXXXXXXX.p8>"
    [ -f "$P8" ] || die "no such file: $P8"

    # App Store Connect names the download AuthKey_<KEYID>.p8
    KEY_ID="$(basename "$P8" .p8)"
    KEY_ID="${KEY_ID#AuthKey_}"

    echo "NOTARY_KEY_ID:   $KEY_ID"
    echo "NOTARY_ISSUER_ID: shown above the key table at"
    echo "                  https://appstoreconnect.apple.com/access/integrations/api"
    echo "                  (a UUID, the same for every key on your team)"
    echo
    echo "NOTARY_KEY:"
    base64 -i "$P8" | copy
    ;;

*)
    sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
