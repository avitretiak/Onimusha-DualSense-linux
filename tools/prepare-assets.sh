#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
DOTNET=${DOTNET:-dotnet}

if [ "$#" -lt 1 ]; then
    echo "Usage: $0 GAME_DIRECTORY [generator options...]" >&2
    exit 2
fi
GAME=$(CDPATH= cd -- "$1" && pwd)
shift
[ -f "$GAME/OnimushaWotS.exe" ] || {
    echo "Missing OnimushaWotS.exe in $GAME" >&2
    exit 1
}
command -v "$DOTNET" >/dev/null 2>&1 || {
    echo "dotnet 10 runtime is required for asset post-processing" >&2
    exit 1
}

has_option() {
    wanted=$1
    shift
    for option in "$@"; do
        [ "$option" = "$wanted" ] && return 0
    done
    return 1
}

if [ -f "$SCRIPT_DIR/asset-generator/OnimushaDualSense.dll" ]; then
    MODE=package
    GENERATOR_DIR="$SCRIPT_DIR/asset-generator"
    DATA_DIR="$SCRIPT_DIR/data"
else
    MODE=repo
    GENERATOR_DIR="$REPO_ROOT/OnimushaDualSense"
    DATA_DIR="$REPO_ROOT/OnimushaDualSense/bin/Release/net10.0/data"
fi

PACKAGE_VERSION=
if [ -f "$SCRIPT_DIR/RELEASE-METADATA.json" ]; then
    PACKAGE_VERSION=$(awk -F'"' '/"version"/ { print $4; exit }' "$SCRIPT_DIR/RELEASE-METADATA.json")
fi

PAK_TOOL=${REE_PAK_CLI:-}
DECODER=${VGMSTREAM_CLI:-}
if [ -z "$PAK_TOOL" ] && command -v ree-pak-cli >/dev/null 2>&1; then
    PAK_TOOL=$(command -v ree-pak-cli)
fi
if [ -z "$DECODER" ] && command -v vgmstream-cli >/dev/null 2>&1; then
    DECODER=$(command -v vgmstream-cli)
fi
if ! has_option --pak-tool "$@"; then
    [ -z "$PAK_TOOL" ] || set -- "$@" --pak-tool "$PAK_TOOL"
fi
if ! has_option --decoder "$@"; then
    [ -z "$DECODER" ] || set -- "$@" --decoder "$DECODER"
fi
pak_ready=0
decoder_ready=0
has_option --pak-tool "$@" && pak_ready=1
has_option --decoder "$@" && decoder_ready=1
if [ "$pak_ready" -eq 0 ] || [ "$decoder_ready" -eq 0 ]; then
    WINE=${WINE:-wine}
    command -v "$WINE" >/dev/null 2>&1 || {
        echo "wine is required when native RE Engine tools are not supplied" >&2
        echo "Set REE_PAK_CLI and VGMSTREAM_CLI to avoid Wine." >&2
        exit 1
    }
fi

TARGET_DATA="$GAME/reframework/data"
MARKER="$TARGET_DATA/.onimusha_dualsense_assets"
if { [ -e "$TARGET_DATA/onimusha_dualsense_native.bin" ] || [ -e "$TARGET_DATA/waves" ]; } &&
   [ ! -f "$MARKER" ]; then
    echo "Refusing to replace unowned files under $TARGET_DATA; remove them or create a mod-owned install first." >&2
    exit 1
fi

if [ -n "$PACKAGE_VERSION" ] &&
   [ -f "$MARKER" ] &&
   [ "$(cat "$MARKER")" = "$PACKAGE_VERSION" ] &&
   [ -f "$TARGET_DATA/onimusha_dualsense_native.bin" ] &&
   [ -d "$TARGET_DATA/waves" ]; then
    printf 'Local assets for release %s are already installed.\n' "$PACKAGE_VERSION"
    exit 0
fi

run_generator() {
    if [ "$MODE" = package ]; then
        "$DOTNET" "$GENERATOR_DIR/OnimushaDualSense.dll" "$@"
    else
        PROJECT="$REPO_ROOT/OnimushaDualSense/OnimushaDualSense.csproj"
        CONFIG="$REPO_ROOT/NuGet.Config"
        "$DOTNET" restore "$PROJECT" --configfile "$CONFIG"
        "$DOTNET" build "$PROJECT" -c Release --no-restore --configfile "$CONFIG"
        "$DOTNET" run --project "$PROJECT" -c Release --no-build -- "$@"
    fi
}

run_generator prepare-assets --game "$GAME" "$@"
[ -f "$DATA_DIR/native_catalog.bin" ] || {
    echo "Asset generation produced no catalog: $DATA_DIR" >&2
    exit 1
}
[ -d "$DATA_DIR/waves" ] || {
    echo "Asset generation produced no waves: $DATA_DIR/waves" >&2
    exit 1
}

TMP=$(mktemp -d "${TMPDIR:-/tmp}/onimusha-assets.XXXXXX")
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
mkdir -p "$TMP/waves"
cp "$DATA_DIR/native_catalog.bin" "$TMP/onimusha_dualsense_native.bin"
set -- "$DATA_DIR/waves"/*.wav
[ -f "$1" ] || { echo "No generated waves found in $DATA_DIR/waves" >&2; exit 1; }
cp "$@" "$TMP/waves/"

mkdir -p "$TARGET_DATA"
rm -rf "$TARGET_DATA/waves"
cp "$TMP/onimusha_dualsense_native.bin" "$TARGET_DATA/"
cp -R "$TMP/waves" "$TARGET_DATA/"
printf '%s\n' "${PACKAGE_VERSION:-developer}" > "$MARKER"
printf 'Generated and installed local assets under %s\n' "$TARGET_DATA"
