#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
MODE=${1:-run}
LOCK=${ONIMUSHA_HIDRELAY_LOCK:-${XDG_RUNTIME_DIR:-/tmp}/onimusha-hidrelay.lock}
PIDFILE=${ONIMUSHA_HIDRELAY_PIDFILE:-${XDG_RUNTIME_DIR:-/tmp}/onimusha-hidrelay.pid}
LOG=${ONIMUSHA_HIDRELAY_LOG:-${XDG_RUNTIME_DIR:-/tmp}/onimusha-hidrelay.log}

pid_is_running() {
    case "${1:-}" in
        ''|*[!0-9]*) return 1 ;;
    esac
    kill -0 "$1" 2>/dev/null
}

if [ "$MODE" = stop ]; then
    if [ ! -f "$PIDFILE" ]; then exit 0; fi
    pid=$(cat "$PIDFILE" 2>/dev/null || true)
    if pid_is_running "$pid"; then
        command_line=$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null || true)
        case "$command_line" in
            *onimusha_hidrelay*) kill "$pid" 2>/dev/null || true ;;
        esac
        i=0
        while pid_is_running "$pid" && [ "$i" -lt 20 ]; do
            sleep 0.1
            i=$((i + 1))
        done
        if pid_is_running "$pid"; then kill -KILL "$pid" 2>/dev/null || true; fi
    fi
    rm -f "$PIDFILE"
    exit 0
fi

case "$MODE" in
    run|start) ;;
    *) printf '%s\n' "Usage: $0 [run|start|stop]" >&2; exit 2 ;;
esac

if [ -n "${ONIMUSHA_HIDRELAY:-}" ]; then
    RELAY=$ONIMUSHA_HIDRELAY
elif [ -x "$SCRIPT_DIR/onimusha_hidrelay" ]; then
    RELAY=$SCRIPT_DIR/onimusha_hidrelay
else
    RELAY=$ROOT/build/hidrelay/onimusha_hidrelay
fi

[ -x "$RELAY" ] || {
    printf '%s\n' "Missing relay executable: $RELAY (set ONIMUSHA_HIDRELAY or place it beside this script)" >&2
    exit 1
}
command -v flock >/dev/null 2>&1 || {
    printf '%s\n' "Missing required command: flock (install util-linux)" >&2
    exit 1
}

if [ "$MODE" = start ]; then
    if [ -f "$PIDFILE" ]; then
        pid=$(cat "$PIDFILE" 2>/dev/null || true)
        if pid_is_running "$pid"; then exit 0; fi
        rm -f "$PIDFILE"
    fi
    mkdir -p "$(dirname "$PIDFILE")" "$(dirname "$LOG")"
    export ONIMUSHA_HIDRELAY_EXEC=$RELAY ONIMUSHA_HIDRELAY_PIDFILE=$PIDFILE
    (
        exec flock -n "$LOCK" sh -c '
            printf "%s\n" "$$" > "$ONIMUSHA_HIDRELAY_PIDFILE"
            exec "$ONIMUSHA_HIDRELAY_EXEC"
        '
    ) >>"$LOG" 2>&1 </dev/null &
    launcher=$!
    i=0
    while [ "$i" -lt 10 ]; do
        if [ -f "$PIDFILE" ]; then
            pid=$(cat "$PIDFILE" 2>/dev/null || true)
            if pid_is_running "$pid"; then exit 0; fi
        fi
        if ! pid_is_running "$launcher"; then break; fi
        sleep 0.1
        i=$((i + 1))
    done
    if ! flock -n "$LOCK" true 2>/dev/null; then exit 0; fi
    printf '%s\n' "Relay failed to start; see $LOG" >&2
    exit 1
fi

exec flock -n "$LOCK" "$RELAY"
