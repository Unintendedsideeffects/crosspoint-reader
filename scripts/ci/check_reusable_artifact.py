#!/usr/bin/env python3
"""Check if a GitHub Actions artifact can be reused from a prior run of the same commit.

Environment variables (required):
  GITHUB_REPOSITORY  - owner/repo
  GH_TOKEN           - GitHub token for API access
  ARTIFACT_NAME      - exact artifact name to search for
  GITHUB_RUN_ID      - current workflow run ID (excluded from candidates)
  GITHUB_SHA         - commit SHA to match
  GITHUB_OUTPUT      - path to output file

Outputs (written to GITHUB_OUTPUT):
  found        - "true" or "false"
  artifact_id  - numeric artifact ID (only if found)
  archive_url  - download URL for the artifact zip (only if found)
  source_run_id - workflow run ID that produced the artifact (only if found)
"""

import json
import os
import urllib.parse
import urllib.request


def _api_get(url: str, token: str) -> dict:
    request = urllib.request.Request(
        url,
        headers={
            "Authorization": f"Bearer {token}",
            "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28",
        },
    )
    with urllib.request.urlopen(request) as response:
        return json.load(response)


def main() -> None:
    repo = os.environ["GITHUB_REPOSITORY"]
    token = os.environ["GH_TOKEN"]
    artifact_name = os.environ["ARTIFACT_NAME"]
    current_run_id = int(os.environ["GITHUB_RUN_ID"])
    sha = os.environ["GITHUB_SHA"]
    output_path = os.environ["GITHUB_OUTPUT"]

    # Determine the current workflow's ID so we only reuse artifacts produced
    # by the same workflow file.  Artifacts from other workflows (e.g. the
    # profile-matrix-test workflow) share identical names but store firmware
    # under dot-prefix paths that actions/upload-artifact silently drops when
    # include-hidden-files is false, resulting in corrupt re-uploads.
    current_run = _api_get(
        f"https://api.github.com/repos/{repo}/actions/runs/{current_run_id}",
        token,
    )
    current_workflow_id = current_run["workflow_id"]

    # Collect all run IDs that belong to this exact workflow and this commit.
    runs_data = _api_get(
        f"https://api.github.com/repos/{repo}/actions/workflows/{current_workflow_id}"
        f"/runs?head_sha={urllib.parse.quote(sha)}&per_page=100",
        token,
    )
    valid_run_ids = {r["id"] for r in runs_data.get("workflow_runs", [])}

    payload = _api_get(
        f"https://api.github.com/repos/{repo}/actions/artifacts"
        f"?name={urllib.parse.quote(artifact_name)}&per_page=100",
        token,
    )

    candidates = []
    for artifact in payload.get("artifacts", []):
        if artifact.get("expired"):
            continue
        run = artifact.get("workflow_run") or {}
        run_id = int(run.get("id", 0))
        if run_id == current_run_id:
            continue
        if run.get("head_sha") != sha:
            continue
        if run_id not in valid_run_ids:
            print(
                f"Skipping artifact {artifact['id']} from run {run_id}: "
                "produced by a different workflow."
            )
            continue
        candidates.append(artifact)

    selected = max(candidates, key=lambda a: a.get("id", 0), default=None)

    with open(output_path, "a", encoding="utf-8") as out:
        if selected is None:
            out.write("found=false\n")
            print(
                f"No reusable '{artifact_name}' artifact found for {sha[:7]}."
            )
        else:
            run = selected.get("workflow_run") or {}
            out.write("found=true\n")
            out.write(f"artifact_id={selected['id']}\n")
            out.write(f"archive_url={selected['archive_download_url']}\n")
            out.write(f"source_run_id={run.get('id', '')}\n")
            print(
                f"Reusing '{artifact_name}' id={selected['id']} "
                f"from run={run.get('id', 'unknown')}"
            )


if __name__ == "__main__":
    main()
