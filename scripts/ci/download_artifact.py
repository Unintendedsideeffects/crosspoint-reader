#!/usr/bin/env python3
"""Download a GitHub Actions artifact from its archive URL and extract it.

Environment variables (required):
  GH_TOKEN    - GitHub token for API access
  ARCHIVE_URL - artifact archive download URL
  OUTPUT_DIR  - directory to extract into (created if missing, cleared if exists)
"""

import io
import os
import shutil
import urllib.request
import zipfile
from pathlib import Path


class _NoAuthOnRedirectHandler(urllib.request.HTTPRedirectHandler):
    """Strip Authorization header before following cross-origin redirects.

    GitHub's artifact zip endpoint redirects to Azure Blob Storage with a
    pre-signed SAS URL. Azure rejects requests that carry a Bearer token
    alongside its own SAS query-param auth, returning HTTP 401.
    """

    def redirect_request(
        self, req, fp, code, msg, headers, newurl
    ):
        new_req = super().redirect_request(req, fp, code, msg, headers, newurl)
        if new_req is not None:
            new_req.remove_header("Authorization")
        return new_req


def main() -> None:
    archive_url = os.environ["ARCHIVE_URL"]
    token = os.environ["GH_TOKEN"]
    output_dir = os.environ.get("OUTPUT_DIR", "artifact")

    request = urllib.request.Request(
        archive_url,
        headers={
            "Authorization": f"Bearer {token}",
            "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28",
        },
    )

    opener = urllib.request.build_opener(_NoAuthOnRedirectHandler)
    with opener.open(request) as response:
        zip_bytes = response.read()

    dest = Path(output_dir)
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)

    with zipfile.ZipFile(io.BytesIO(zip_bytes)) as archive:
        archive.extractall(dest)

    print(f"Downloaded artifact to {dest}:")
    for f in sorted(dest.rglob("*")):
        if f.is_file():
            size_kb = f.stat().st_size / 1024
            print(f"  {f.relative_to(dest)} ({size_kb:.1f} KB)")


if __name__ == "__main__":
    main()
