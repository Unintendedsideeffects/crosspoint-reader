#!/usr/bin/env python3
"""Prune old GitHub Actions artifacts.

Deletes artifacts that are BOTH:
  - Older than MAX_AGE_DAYS (default: 14)
  - Have at least MIN_KEEP newer siblings with the same artifact name

This keeps recent builds available and ensures at least MIN_KEEP artifacts
per name are always retained, even if they're old.

Environment variables (required):
  GITHUB_REPOSITORY - owner/repo
  GH_TOKEN          - GitHub token with actions:write scope

Environment variables (optional):
  MAX_AGE_DAYS  - age threshold in days (default: 14)
  MIN_KEEP      - minimum artifacts to retain per name (default: 5)
  DRY_RUN       - set to "true" to preview without deleting
"""

import json
import os
import urllib.request
from collections import defaultdict
from datetime import datetime, timedelta, timezone


def api(url: str, method: str = "GET") -> dict | None:
    token = os.environ["GH_TOKEN"]
    request = urllib.request.Request(
        url,
        method=method,
        headers={
            "Authorization": f"Bearer {token}",
            "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28",
        },
    )
    try:
        with urllib.request.urlopen(request) as response:
            if method == "DELETE":
                return None
            return json.load(response)
    except urllib.error.HTTPError as exc:
        if method == "DELETE" and exc.code == 404:
            return None  # already gone
        raise


def main() -> None:
    repo = os.environ["GITHUB_REPOSITORY"]
    max_age_days = int(os.environ.get("MAX_AGE_DAYS", "14"))
    min_keep = int(os.environ.get("MIN_KEEP", "5"))
    dry_run = os.environ.get("DRY_RUN", "").lower() == "true"

    # Fetch all artifacts (paginated)
    all_artifacts: list[dict] = []
    page = 1
    while True:
        url = (
            f"https://api.github.com/repos/{repo}/actions/artifacts"
            f"?per_page=100&page={page}"
        )
        payload = api(url)
        batch = payload.get("artifacts", []) if payload else []
        if not batch:
            break
        all_artifacts.extend(batch)
        page += 1

    print(f"Fetched {len(all_artifacts)} total artifacts.")

    # Group non-expired artifacts by name
    groups: dict[str, list[dict]] = defaultdict(list)
    for artifact in all_artifacts:
        if not artifact.get("expired"):
            groups[artifact["name"]].append(artifact)

    cutoff = datetime.now(timezone.utc) - timedelta(days=max_age_days)
    deleted = 0
    skipped = 0

    for name, items in sorted(groups.items()):
        # Sort newest-first by creation date
        items.sort(key=lambda a: a["created_at"], reverse=True)

        for i, artifact in enumerate(items):
            # Always keep the MIN_KEEP most recent
            if i < min_keep:
                continue

            created = datetime.fromisoformat(
                artifact["created_at"].replace("Z", "+00:00")
            )
            if created >= cutoff:
                skipped += 1
                continue

            artifact_id = artifact["id"]
            age_days = (datetime.now(timezone.utc) - created).days

            if dry_run:
                print(
                    f"[DRY RUN] Would delete: {name} "
                    f"(id={artifact_id}, age={age_days}d, "
                    f"position={i + 1}/{len(items)})"
                )
                deleted += 1
            else:
                api(
                    f"https://api.github.com/repos/{repo}"
                    f"/actions/artifacts/{artifact_id}",
                    method="DELETE",
                )
                print(
                    f"Deleted: {name} "
                    f"(id={artifact_id}, age={age_days}d, "
                    f"position={i + 1}/{len(items)})"
                )
                deleted += 1

    action = "would delete" if dry_run else "deleted"
    print(f"\nDone. {action.capitalize()} {deleted} artifacts, skipped {skipped}.")


if __name__ == "__main__":
    main()
