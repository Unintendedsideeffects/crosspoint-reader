#!/usr/bin/env bash

set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: scripts/run_gemini_task.sh [options]

Run Gemini CLI in non-interactive mode with timeout/retry guardrails.

Options:
  --prompt <text>             Prompt text
  --prompt-file <path>        Read prompt from file
  --workdir <path>            Working directory (default: current directory)
  --model <name>              Gemini model override (default: CLI default)
  --output-format <fmt>       text|json|stream-json (default: stream-json)
  --timeout <seconds>         Attempt timeout in seconds (default: 900)
  --retries <count>           Retry count after first attempt (default: 1)
  --backoff <seconds>         Base sleep between retries (default: 5)
  --output <path>             Save full stdout/stderr log to file
  -h, --help                  Show this help

Examples:
  scripts/run_gemini_task.sh --prompt "Reply with OK" --output-format text
  scripts/run_gemini_task.sh --prompt-file /tmp/task.txt --timeout 1200 --retries 2
EOF
}

die() {
  echo "run_gemini_task.sh: $*" >&2
  exit 1
}

is_retryable_failure() {
  local log_file="$1"
  rg -qi \
    "exhausted your capacity|capacity|overloaded|rate limit|too many requests|429|temporar|timed out|timeout|network error|connection reset|retrying" \
    "$log_file"
}

PROMPT_TEXT=""
PROMPT_FILE=""
WORKDIR="$(pwd)"
MODEL=""
OUTPUT_FORMAT="stream-json"
TIMEOUT_SECONDS=900
RETRIES=1
BACKOFF_SECONDS=5
OUTPUT_LOG=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prompt)
      [[ $# -ge 2 ]] || die "--prompt requires a value"
      PROMPT_TEXT="$2"
      shift 2
      ;;
    --prompt-file)
      [[ $# -ge 2 ]] || die "--prompt-file requires a value"
      PROMPT_FILE="$2"
      shift 2
      ;;
    --workdir)
      [[ $# -ge 2 ]] || die "--workdir requires a value"
      WORKDIR="$2"
      shift 2
      ;;
    --model)
      [[ $# -ge 2 ]] || die "--model requires a value"
      MODEL="$2"
      shift 2
      ;;
    --output-format)
      [[ $# -ge 2 ]] || die "--output-format requires a value"
      OUTPUT_FORMAT="$2"
      shift 2
      ;;
    --timeout)
      [[ $# -ge 2 ]] || die "--timeout requires a value"
      TIMEOUT_SECONDS="$2"
      shift 2
      ;;
    --retries)
      [[ $# -ge 2 ]] || die "--retries requires a value"
      RETRIES="$2"
      shift 2
      ;;
    --backoff)
      [[ $# -ge 2 ]] || die "--backoff requires a value"
      BACKOFF_SECONDS="$2"
      shift 2
      ;;
    --output)
      [[ $# -ge 2 ]] || die "--output requires a value"
      OUTPUT_LOG="$2"
      shift 2
      ;;
    -h|--help)
      show_help
      exit 0
      ;;
    *)
      die "unknown argument: $1"
      ;;
  esac
done

[[ -d "$WORKDIR" ]] || die "workdir does not exist: $WORKDIR"

if [[ -n "$PROMPT_FILE" ]]; then
  [[ -f "$PROMPT_FILE" ]] || die "prompt file does not exist: $PROMPT_FILE"
  PROMPT_TEXT="$(cat "$PROMPT_FILE")"
elif [[ -z "$PROMPT_TEXT" ]]; then
  if [[ -t 0 ]]; then
    die "no prompt provided; use --prompt, --prompt-file, or stdin"
  fi
  PROMPT_TEXT="$(cat)"
fi

[[ -n "$PROMPT_TEXT" ]] || die "prompt is empty"

if [[ -n "$OUTPUT_LOG" ]]; then
  : >"$OUTPUT_LOG"
fi

cmd=(
  gemini
  --prompt "$PROMPT_TEXT"
  --approval-mode yolo
  --sandbox false
  --output-format "$OUTPUT_FORMAT"
)

if [[ -n "$MODEL" ]]; then
  cmd+=(--model "$MODEL")
fi

attempt=1
max_attempts=$((RETRIES + 1))
while (( attempt <= max_attempts )); do
  log_tmp="$(mktemp)"
  set +e
  (
    cd "$WORKDIR"
    timeout --signal=TERM --kill-after=10s "${TIMEOUT_SECONDS}s" "${cmd[@]}"
  ) >"$log_tmp" 2>&1
  exit_code=$?
  set -e

  cat "$log_tmp"
  if [[ -n "$OUTPUT_LOG" ]]; then
    cat "$log_tmp" >>"$OUTPUT_LOG"
  fi

  if [[ $exit_code -eq 0 ]]; then
    rm -f "$log_tmp"
    exit 0
  fi

  retry_reason=""
  if [[ $exit_code -eq 124 || $exit_code -eq 137 ]]; then
    retry_reason="timeout"
  elif is_retryable_failure "$log_tmp"; then
    retry_reason="capacity/network"
  fi

  if (( attempt < max_attempts )) && [[ -n "$retry_reason" ]]; then
    sleep_for=$((BACKOFF_SECONDS * attempt))
    echo "run_gemini_task.sh: attempt ${attempt}/${max_attempts} failed (${retry_reason}), retrying in ${sleep_for}s" >&2
    rm -f "$log_tmp"
    sleep "$sleep_for"
    attempt=$((attempt + 1))
    continue
  fi

  rm -f "$log_tmp"
  exit "$exit_code"
done
