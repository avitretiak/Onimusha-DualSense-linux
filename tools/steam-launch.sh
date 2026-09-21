#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if [ "$#" -eq 0 ]; then
    echo "Usage: sh ./steam-launch.sh %command%" >&2
    exit 2
fi

# Steam expands %command% into the normal Proton command. The helper is only a
# fallback for native HID output; failure to start it must not block the game.
RUNTIME_DIR=${XDG_RUNTIME_DIR:-/tmp}
PIDFILE=$RUNTIME_DIR/onimusha-hidrelay-steam-$$.pid
LOG=$RUNTIME_DIR/onimusha-hidrelay-steam-$$.log
HELPER_STARTED=0

cleanup() {
    if [ "$HELPER_STARTED" -eq 1 ]; then
        ONIMUSHA_HIDRELAY_PIDFILE="$PIDFILE" ONIMUSHA_HIDRELAY_LOG="$LOG" \
            sh "$ROOT/start-hidrelay.sh" stop || true
    fi
    rm -f "$LOG"
}
trap cleanup EXIT

if ONIMUSHA_HIDRELAY_PIDFILE="$PIDFILE" ONIMUSHA_HIDRELAY_LOG="$LOG" \
    sh "$ROOT/start-hidrelay.sh" start >/dev/null 2>&1 && [ -f "$PIDFILE" ]; then
    HELPER_STARTED=1
fi

"$@"
status=$?
exit "$status"
