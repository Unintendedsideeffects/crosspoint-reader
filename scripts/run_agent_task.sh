#!/usr/bin/env bash

set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: scripts/run_agent_task.sh [options]

Unified wrapper for tuned Claude/Gemini invocations.

Options:
  --provider <name>        claude|gemini|auto (default: auto)
  --profile <name>         small|default|deep (default: default)
  --prompt <text>          Prompt text
  --prompt-file <path>     Read prompt from file
  --workdir <path>         Working directory for agent execution
  --model <name>           Override model passed to chosen provider
  --output-format <fmt>    text|json|stream-json (default: stream-json)
  --output <path>          Save wrapper output log
  -h, --help               Show help

Notes:
  - In auto mode: Claude is attempted first, then Gemini on failure.
  - Profile controls timeout/retry/effort defaults.
EOF
}

die() {
  echo "run_agent_task.sh: $*" >&2
  exit 1
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLAUDE_SCRIPT="$SCRIPT_DIR/run_claude_task.sh"
GEMINI_SCRIPT="$SCRIPT_DIR/run_gemini_task.sh"

[[ -x "$CLAUDE_SCRIPT" ]] || die "missing executable: $CLAUDE_SCRIPT"
[[ -x "$GEMINI_SCRIPT" ]] || die "missing executable: $GEMINI_SCRIPT"

PROVIDER="auto"
PROFILE="default"
PROMPT_TEXT=""
PROMPT_FILE=""
WORKDIR="$(pwd)"
MODEL_OVERRIDE=""
OUTPUT_FORMAT="stream-json"
OUTPUT_LOG=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --provider)
      [[ $# -ge 2 ]] || die "--provider requires a value"
      PROVIDER="$2"
      shift 2
      ;;
    --profile)
      [[ $# -ge 2 ]] || die "--profile requires a value"
      PROFILE="$2"
      shift 2
      ;;
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
      MODEL_OVERRIDE="$2"
      shift 2
      ;;
    --output-format)
      [[ $# -ge 2 ]] || die "--output-format requires a value"
      OUTPUT_FORMAT="$2"
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
elif [[ -z "$PROMPT_TEXT" ]]; then
  if [[ -t 0 ]]; then
    die "no prompt provided; use --prompt, --prompt-file, or stdin"
  fi
fi

prompt_tmp="$(mktemp)"
cleanup() {
  rm -f "$prompt_tmp"
}
trap cleanup EXIT

if [[ -n "$PROMPT_FILE" ]]; then
  cp "$PROMPT_FILE" "$prompt_tmp"
elif [[ -n "$PROMPT_TEXT" ]]; then
  printf "%s" "$PROMPT_TEXT" >"$prompt_tmp"
else
  cat >"$prompt_tmp"
fi

claude_effort="medium"
claude_timeout=1200
claude_retries=1
claude_backoff=5
claude_fallback="haiku"

gemini_timeout=900
gemini_retries=1
gemini_backoff=5

case "$PROFILE" in
  small)
    claude_effort="low"
    claude_timeout=420
    claude_retries=2
    claude_backoff=4
    claude_fallback="haiku"
    gemini_timeout=360
    gemini_retries=2
    gemini_backoff=4
    ;;
  default)
    ;;
  deep)
    claude_effort="high"
    claude_timeout=2400
    claude_retries=1
    claude_backoff=8
    claude_fallback="sonnet"
    gemini_timeout=1800
    gemini_retries=1
    gemini_backoff=8
    ;;
  *)
    die "unknown profile: $PROFILE (use small|default|deep)"
    ;;
esac

if [[ -n "$OUTPUT_LOG" ]]; then
  : >"$OUTPUT_LOG"
fi

run_claude() {
  args=(
    --prompt-file "$prompt_tmp"
    --workdir "$WORKDIR"
    --effort "$claude_effort"
    --fallback-model "$claude_fallback"
    --output-format "$OUTPUT_FORMAT"
    --timeout "$claude_timeout"
    --retries "$claude_retries"
    --backoff "$claude_backoff"
  )
  if [[ -n "$MODEL_OVERRIDE" ]]; then
    args+=(--model "$MODEL_OVERRIDE")
  fi
  if [[ -n "$OUTPUT_LOG" ]]; then
    args+=(--output "$OUTPUT_LOG")
  fi
  "$CLAUDE_SCRIPT" "${args[@]}"
}

run_gemini() {
  args=(
    --prompt-file "$prompt_tmp"
    --workdir "$WORKDIR"
    --output-format "$OUTPUT_FORMAT"
    --timeout "$gemini_timeout"
    --retries "$gemini_retries"
    --backoff "$gemini_backoff"
  )
  if [[ -n "$MODEL_OVERRIDE" ]]; then
    args+=(--model "$MODEL_OVERRIDE")
  fi
  if [[ -n "$OUTPUT_LOG" ]]; then
    args+=(--output "$OUTPUT_LOG")
  fi
  "$GEMINI_SCRIPT" "${args[@]}"
}

case "$PROVIDER" in
  claude)
    run_claude
    ;;
  gemini)
    run_gemini
    ;;
  auto)
    set +e
    run_claude
    claude_exit=$?
    set -e
    if [[ $claude_exit -eq 0 ]]; then
      exit 0
    fi
    echo "run_agent_task.sh: Claude failed (exit ${claude_exit}), falling back to Gemini" >&2
    run_gemini
    ;;
  *)
    die "unknown provider: $PROVIDER (use claude|gemini|auto)"
    ;;
esac
