# Agent Invocation Tuning

This project uses tuned wrappers for Claude and Gemini to reduce silent hangs and improve reproducibility.

## Scripts

- `scripts/run_claude_task.sh`
- `scripts/run_gemini_task.sh`
- `scripts/run_agent_task.sh` (auto fallback)

All wrappers provide:
- hard per-attempt timeouts (`timeout`)
- retry/backoff for capacity/network failures
- optional full log capture (`--output`)
- fixed non-interactive mode with explicit flags

## Recommended Entry Point

Use the unified wrapper:

```bash
scripts/run_agent_task.sh --provider auto --profile default --prompt-file /tmp/task.txt
```

`auto` tries Claude first, then Gemini if Claude fails.

## Profiles

- `small`: fast iterations, short timeout, extra retries.
- `default`: balanced for normal implementation tasks.
- `deep`: longer timeout and higher Claude effort for larger refactors.

## Examples

Claude only:

```bash
scripts/run_agent_task.sh \
  --provider claude \
  --profile deep \
  --output-format text \
  --prompt "Summarize pending feature-gating work in 5 bullets."
```

Gemini only:

```bash
scripts/run_agent_task.sh \
  --provider gemini \
  --profile small \
  --output-format text \
  --prompt "Return exactly OK_GEMINI"
```

Auto with log:

```bash
scripts/run_agent_task.sh \
  --provider auto \
  --profile default \
  --prompt-file /tmp/task.txt \
  --output /tmp/agent-run.log
```

## Prompt Structure That Performs Better

For both providers, keep prompts scoped and bounded:

1. repository path and branch
2. exact file scope
3. behavior constraints
4. explicit test commands
5. explicit commit/no-commit instruction
6. exact expected return format

Avoid broad multi-goal prompts for a single run. Split large work into sequential slices.

## Notes

- For local visibility and progress, wrappers default to `stream-json`.
- For human-friendly terminal output, pass `--output-format text`.
- If a task stalls repeatedly, switch to `small` profile and narrow scope.
