#!/usr/bin/env bash
set -euo pipefail

if (($# == 0)); then
    printf 'usage: %s COMMAND [ARGUMENT...\n' "$0" >&2
    exit 2
fi

redact_env()
{
    while IFS= read -r line; do
        case "$line" in
            *_KEY=*|*_TOKEN=*|*_PASSWORD=*|*_SECRET=*)
                printf '%s=<redacted>\n' "${line%%=*}"
                ;;
            *) printf '%s\n' "$line" ;;
        esac
    done
}

capture_env()
{
    sort | redact_env
}

capture_proc_env()
{
    tr '\0' '\n' | sort | redact_env
}


out_dir=${ONIMUSHA_TRACE_DIR:-./work/diagnostics}
mkdir -p "$out_dir"
out="$out_dir/proton-overlay-$(date +%Y%m%d-%H%M%S).log"

{
    printf 'trace_started=%s\n' "$(date --iso-8601=seconds)"
    printf 'cwd=%q\n' "$PWD"
    printf 'command='; printf '%q ' "$@"; printf '\n\n'
    env | capture_env
    printf '\n%s\n' '[processes-before]'
    ps -eo pid,ppid,stat,etime,args
} >"$out"

"$@" >>"$out" 2>&1 &
pid=$!
trap 'kill "$pid" 2>/dev/null || true' INT TERM

while kill -0 "$pid" 2>/dev/null; do
    {
        printf '\n[process-snapshot %s]\n' "$(date --iso-8601=seconds)"
        ps -eo pid,ppid,stat,etime,args
        if [[ -r "/proc/$pid/environ" ]]; then
            capture_proc_env <"/proc/$pid/environ"
        fi
        if [[ -r "/proc/$pid/maps" ]]; then
            printf '\n[root-maps]\n'
            cat "/proc/$pid/maps"
        fi
    } >>"$out"
    sleep 1
done

wait "$pid"
status=$?
printf '\ntrace_exit=%d\n' "$status" >>"$out"
printf '%s\n' "$out"
exit "$status"
