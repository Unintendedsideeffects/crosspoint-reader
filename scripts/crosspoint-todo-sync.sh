#!/usr/bin/env bash
#
# CrossPoint TODO Sync - Bidirectional sync between Obsidian and CrossPoint Reader
#
# Usage:
#   crosspoint-todo-sync.sh push [DATE]  - Push Obsidian daily note to device
#   crosspoint-todo-sync.sh pull [DATE]  - Pull and merge device changes to Obsidian
#   crosspoint-todo-sync.sh watch        - Continuously sync when device available
#
# Configuration (environment variables):
#   CROSSPOINT_HOST    - Device hostname/IP (default: crosspoint.local)
#   CHECK_INTERVAL     - Watch mode poll interval in seconds (default: 30)
#   VAULT_ROOT         - Obsidian vault root (auto-detected from script location)
#   DAILY_NOTES_DIR    - Daily notes directory (default: $VAULT_ROOT/daily)
#
# Installation:
#   Copy to: YourVault/Scripts/.watchers/crosspoint-todo-sync.sh
#   The script auto-detects vault path from its location.
#

set -euo pipefail

# Configuration with defaults
CROSSPOINT_HOST="${CROSSPOINT_HOST:-crosspoint.local}"
CHECK_INTERVAL="${CHECK_INTERVAL:-30}"

# Auto-detect vault root from script location
# Expected: YourVault/Scripts/.watchers/crosspoint-todo-sync.sh
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -z "${VAULT_ROOT:-}" ]]; then
    # Try to detect vault root (two levels up from .watchers)
    if [[ "$(basename "$SCRIPT_DIR")" == ".watchers" ]]; then
        VAULT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
    else
        # Fallback: assume script is in vault root
        VAULT_ROOT="$SCRIPT_DIR"
    fi
fi

DAILY_NOTES_DIR="${DAILY_NOTES_DIR:-$VAULT_ROOT/daily}"
DEVICE_DAILY_PATH="/daily"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() { echo -e "${BLUE}[INFO]${NC} $*"; }
log_success() { echo -e "${GREEN}[OK]${NC} $*"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*" >&2; }

# Get today's date in YYYY-MM-DD format
get_today() {
    date +%Y-%m-%d
}

# Check if device is reachable
check_device() {
    curl -sf --connect-timeout 2 --max-time 5 \
        "http://${CROSSPOINT_HOST}/api/status" >/dev/null 2>&1
}

# Wait for device to come online
wait_for_device() {
    log_info "Waiting for CrossPoint device at ${CROSSPOINT_HOST}..."
    while ! check_device; do
        sleep "$CHECK_INTERVAL"
    done
    log_success "Device is online!"
}

# Push a daily note to the device
# Args: date [quiet]
push_note() {
    local date="${1:-$(get_today)}"
    local quiet="${2:-false}"
    local local_file="$DAILY_NOTES_DIR/${date}.md"

    if [[ ! -f "$local_file" ]]; then
        [[ "$quiet" != "true" ]] && log_error "Local file not found: $local_file"
        return 1
    fi

    [[ "$quiet" != "true" ]] && log_info "Pushing $date.md to device..."

    # Upload via multipart form (30s timeout)
    if curl -sf --max-time 30 -X POST \
        -F "file=@${local_file}" \
        "http://${CROSSPOINT_HOST}/upload?path=${DEVICE_DAILY_PATH}" >/dev/null 2>&1; then
        [[ "$quiet" != "true" ]] && log_success "Pushed $date.md to device"
        return 0
    else
        [[ "$quiet" != "true" ]] && log_error "Failed to push"
        return 1
    fi
}

# Download a daily note from the device
# Args: date [quiet]
download_note() {
    local date="${1:-$(get_today)}"
    local quiet="${2:-false}"
    local device_path="${DEVICE_DAILY_PATH}/${date}.md"
    local temp_file
    temp_file=$(mktemp)

    [[ "$quiet" != "true" ]] && log_info "Downloading $date.md from device..."

    # 30s timeout to prevent hanging
    if curl -sf --max-time 30 "http://${CROSSPOINT_HOST}/download?path=${device_path}" -o "$temp_file" 2>/dev/null; then
        echo "$temp_file"
        return 0
    else
        rm -f "$temp_file"
        return 1
    fi
}

# Extract the TODO section from a daily note file
# Handles frontmatter and multiple sections, returns ## TODO content
# Args: file [tasks_only]
#   tasks_only=true: only return task lines (for building task list)
#   tasks_only=false/omitted: return all lines (preserves structure)
extract_todo_section() {
    local file="$1"
    local tasks_only="${2:-false}"
    local in_todo=false
    local in_frontmatter=false
    local frontmatter_count=0

    while IFS= read -r line || [[ -n "$line" ]]; do
        # Track frontmatter (starts and ends with ---)
        if [[ "$line" == "---" ]]; then
            ((frontmatter_count++))
            if [[ $frontmatter_count -eq 1 ]]; then
                in_frontmatter=true
                continue
            elif [[ $frontmatter_count -eq 2 ]]; then
                in_frontmatter=false
                continue
            fi
        fi
        [[ "$in_frontmatter" == true ]] && continue

        # Detect section headers (## level)
        if [[ "$line" =~ ^##\  ]]; then
            if [[ "$line" == "## TODO" ]]; then
                in_todo=true
            else
                in_todo=false
            fi
            continue
        fi

        # Output lines in TODO section
        if [[ "$in_todo" == true ]]; then
            if [[ "$tasks_only" == "true" ]]; then
                # Only output task lines (- [ ] or - [x] with one space after)
                if [[ "$line" == "- [ ] "* ]] || [[ "$line" == "- [x] "* ]] || [[ "$line" == "- [X] "* ]]; then
                    echo "$line"
                fi
            else
                echo "$line"
            fi
        fi
    done < "$file"
}

# Merge device TODO content into Obsidian file
# Strategy:
#   - Preserve Obsidian frontmatter and non-TODO sections
#   - Replace TODO section with device content (preserves reordering)
#   - Merge completion status (union)
#   - Append new Obsidian tasks not on device
merge_tasks() {
    local obsidian_file="$1"
    local device_file="$2"

    # Build ordered list of Obsidian TODO tasks: "checked|text"
    # Only extract tasks (tasks_only=true) for completion matching
    local -a obsidian_tasks=()
    local -a obsidian_tasks_used=()

    while IFS= read -r line || [[ -n "$line" ]]; do
        if [[ "$line" =~ ^-\ \[\ \]\ (.*)$ ]]; then
            obsidian_tasks+=("0|${BASH_REMATCH[1]}")
            obsidian_tasks_used+=(0)
        elif [[ "$line" =~ ^-\ \[[xX]\]\ (.*)$ ]]; then
            obsidian_tasks+=("1|${BASH_REMATCH[1]}")
            obsidian_tasks_used+=(0)
        fi
    done < <(extract_todo_section "$obsidian_file" "true")

    # Build merged TODO content from device's ## TODO section only
    # Preserves non-task lines (sub-headers, notes within TODO)
    local -a merged_todo=()

    while IFS= read -r line || [[ -n "$line" ]]; do
        if [[ "$line" =~ ^-\ \[\ \]\ (.*)$ ]]; then
            local device_text="${BASH_REMATCH[1]}"
            local device_checked=0

            # Find first unused matching task in Obsidian
            local obsidian_checked=0
            for i in "${!obsidian_tasks[@]}"; do
                if [[ "${obsidian_tasks_used[$i]}" == "0" ]]; then
                    local entry="${obsidian_tasks[$i]}"
                    local text="${entry#*|}"
                    if [[ "$text" == "$device_text" ]]; then
                        obsidian_checked="${entry%%|*}"
                        obsidian_tasks_used[$i]=1
                        break
                    fi
                fi
            done

            if [[ "$obsidian_checked" == "1" || "$device_checked" == "1" ]]; then
                merged_todo+=("- [x] $device_text")
            else
                merged_todo+=("- [ ] $device_text")
            fi

        elif [[ "$line" =~ ^-\ \[[xX]\]\ (.*)$ ]]; then
            local device_text="${BASH_REMATCH[1]}"

            # Mark matching Obsidian task as used
            for i in "${!obsidian_tasks[@]}"; do
                if [[ "${obsidian_tasks_used[$i]}" == "0" ]]; then
                    local entry="${obsidian_tasks[$i]}"
                    local text="${entry#*|}"
                    if [[ "$text" == "$device_text" ]]; then
                        obsidian_tasks_used[$i]=1
                        break
                    fi
                fi
            done

            merged_todo+=("- [x] $device_text")
        else
            # Preserve non-task lines from device (sub-headers, notes, blank lines)
            merged_todo+=("$line")
        fi
    done < <(extract_todo_section "$device_file")

    # Append new tasks from Obsidian (in original order)
    for i in "${!obsidian_tasks[@]}"; do
        if [[ "${obsidian_tasks_used[$i]}" == "0" ]]; then
            local entry="${obsidian_tasks[$i]}"
            local checked="${entry%%|*}"
            local text="${entry#*|}"
            if [[ "$checked" == "1" ]]; then
                merged_todo+=("- [x] $text")
            else
                merged_todo+=("- [ ] $text")
            fi
        fi
    done

    # Now output the full Obsidian file with TODO section replaced
    local in_todo=false
    local todo_written=false
    local in_frontmatter=false
    local frontmatter_count=0

    while IFS= read -r line || [[ -n "$line" ]]; do
        # Track frontmatter
        if [[ "$line" == "---" ]]; then
            ((frontmatter_count++))
            if [[ $frontmatter_count -eq 1 ]]; then
                in_frontmatter=true
            elif [[ $frontmatter_count -eq 2 ]]; then
                in_frontmatter=false
            fi
            echo "$line"
            continue
        fi

        if [[ "$in_frontmatter" == true ]]; then
            echo "$line"
            continue
        fi

        # Detect section headers
        if [[ "$line" =~ ^##\  ]]; then
            if [[ "$line" == "## TODO" ]]; then
                in_todo=true
                echo "$line"
                # Write merged TODO content (skip leading/trailing blank lines)
                local started=false
                local pending_blank=false
                for task in "${merged_todo[@]+"${merged_todo[@]}"}"; do
                    if [[ -z "$task" ]]; then
                        # Defer blank lines to avoid leading/trailing blanks
                        $started && pending_blank=true
                    else
                        $pending_blank && echo ""
                        pending_blank=false
                        started=true
                        echo "$task"
                    fi
                done
                todo_written=true
            else
                # Add single blank line before next section if leaving TODO
                $in_todo && echo ""
                in_todo=false
                echo "$line"
            fi
            continue
        fi

        # Skip original TODO content (we replaced it)
        if [[ "$in_todo" == true ]]; then
            continue
        fi

        echo "$line"
    done < "$obsidian_file"

    # If no TODO section existed, append one
    if [[ "$todo_written" == false ]] && [[ ${#merged_todo[@]} -gt 0 ]]; then
        echo ""
        echo "## TODO"
        echo ""
        for task in "${merged_todo[@]}"; do
            [[ -n "$task" ]] && echo "$task"
        done
    fi
}

# Pull and merge device changes
# Args: date [quiet]
pull_note() {
    local date="${1:-$(get_today)}"
    local quiet="${2:-false}"
    local local_file="$DAILY_NOTES_DIR/${date}.md"

    # Download from device
    local device_file
    if ! device_file=$(download_note "$date" "$quiet"); then
        [[ "$quiet" != "true" ]] && log_error "Failed to download $date.md from device"
        return 1
    fi

    # If local file doesn't exist, just use device version
    if [[ ! -f "$local_file" ]]; then
        log_info "No local file, using device version"
        mkdir -p "$DAILY_NOTES_DIR"
        mv "$device_file" "$local_file"
        log_success "Created $local_file from device"
        return 0
    fi

    # Merge the two versions
    local merged_file
    merged_file=$(mktemp)

    merge_tasks "$local_file" "$device_file" > "$merged_file"

    # Check if there are any changes
    if diff -q "$local_file" "$merged_file" > /dev/null 2>&1; then
        rm -f "$device_file" "$merged_file"
        return 0
    fi

    # Backup original (single .bak, overwritten each merge) and apply
    log_info "Merging device changes for $date..."
    mv "$local_file" "${local_file}.bak"
    mv "$merged_file" "$local_file"

    rm -f "$device_file"
    log_success "Merged changes into $local_file"
    return 0
}

# Watch mode: continuously sync when device is available
watch_mode() {
    log_info "Starting watch mode (interval: ${CHECK_INTERVAL}s)"
    log_info "Vault: $VAULT_ROOT"
    log_info "Daily notes: $DAILY_NOTES_DIR"
    log_info "Device: $CROSSPOINT_HOST"
    echo ""

    local last_push_mtime=0  # Track when we last pushed based on local mtime
    local device_was_offline=true

    while true; do
        if check_device; then
            local today
            today=$(get_today)
            local local_file="$DAILY_NOTES_DIR/${today}.md"
            local pull_ok=false

            # ALWAYS pull first to capture device changes before any push
            if $device_was_offline; then
                log_info "Device online, syncing $today..."
                device_was_offline=false
                pull_note "$today" && pull_ok=true
            else
                # Quiet pull on regular cycles
                pull_note "$today" "true" && pull_ok=true
            fi

            # Only push after successful pull (prevents overwriting device changes)
            if [[ "$pull_ok" == true ]] || [[ ! -f "$local_file" ]]; then
                # Get local file modification time (after pull may have updated it)
                local local_mtime=0
                if [[ -f "$local_file" ]]; then
                    local_mtime=$(stat -c %Y "$local_file" 2>/dev/null || stat -f %m "$local_file" 2>/dev/null || echo 0)
                fi

                # Push if local file changed since last push (includes merge results)
                if [[ "$local_mtime" -gt "$last_push_mtime" ]] && [[ -f "$local_file" ]]; then
                    push_note "$today" "true" && last_push_mtime="$local_mtime"
                fi
            fi
        else
            if ! $device_was_offline; then
                log_warn "Device offline, waiting..."
                device_was_offline=true
            fi
        fi

        sleep "$CHECK_INTERVAL"
    done
}

# Show usage
usage() {
    cat <<EOF
CrossPoint TODO Sync - Bidirectional sync between Obsidian and CrossPoint Reader

Usage: $(basename "$0") <command> [options]

Commands:
  push [DATE]   Push Obsidian daily note to device (default: today)
  pull [DATE]   Pull and merge device changes to Obsidian (default: today)
  watch         Continuously sync when device available

Date format: YYYY-MM-DD (e.g., 2026-01-28)

Environment variables:
  CROSSPOINT_HOST    Device hostname/IP (default: crosspoint.local)
  CHECK_INTERVAL     Watch mode poll interval in seconds (default: 30)
  VAULT_ROOT         Obsidian vault root (auto-detected)
  DAILY_NOTES_DIR    Daily notes directory (default: \$VAULT_ROOT/daily)

Examples:
  $(basename "$0") push              # Push today's note
  $(basename "$0") push 2026-01-28   # Push specific date
  $(basename "$0") pull              # Pull and merge today's note
  $(basename "$0") watch             # Start continuous sync

Installation:
  Copy to: YourVault/Scripts/.watchers/crosspoint-todo-sync.sh
  Make executable: chmod +x crosspoint-todo-sync.sh
EOF
}

# Main entry point
main() {
    local cmd="${1:-}"
    shift || true

    case "$cmd" in
        push)
            push_note "$@"
            ;;
        pull)
            pull_note "$@"
            ;;
        watch)
            watch_mode
            ;;
        -h|--help|help)
            usage
            ;;
        "")
            # Default to watch mode
            watch_mode
            ;;
        *)
            log_error "Unknown command: $cmd"
            echo ""
            usage
            exit 1
            ;;
    esac
}

main "$@"
