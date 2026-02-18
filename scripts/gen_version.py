"""
PlatformIO pre-build script: inject CROSSPOINT_VERSION for dynamic build environments.

Environments handled:
  default   → "<commit_count>-dev"  (git commit count, capped at 5 digits)
  gh_latest → "<commit_count>-dev"  (same, used for the rolling 'latest' OTA channel)
  gh_nightly → "<YYYYMMDD>"         (UTC build date, used for the 'nightly' OTA channel)

All other environments (gh_release, gh_release_rc, slim, custom, …) define
CROSSPOINT_VERSION statically in platformio.ini and are left untouched.

CI can override the computed values via environment variables:
  GIT_COMMIT_COUNT  - integer commit count (overrides git rev-list output)
  BUILD_DATE        - YYYYMMDD string     (overrides current UTC date)
"""

Import("env")  # noqa: F821 – PlatformIO SCons global

import datetime
import os
import subprocess

DYNAMIC_ENVS = {
    "default": "commit_dev",
    "gh_latest": "commit_dev",
    "gh_nightly": "date",
}

env_name = env["PIOENV"]  # noqa: F821

if env_name not in DYNAMIC_ENVS:
    # Static version defined in platformio.ini – nothing to do.
    Return()  # noqa: F821


def get_commit_count() -> str:
    if "GIT_COMMIT_COUNT" in os.environ:
        count = int(os.environ["GIT_COMMIT_COUNT"])
    else:
        try:
            result = subprocess.run(
                ["git", "rev-list", "--count", "HEAD"],
                capture_output=True,
                text=True,
                timeout=5,
            )
            count = int(result.stdout.strip()) if result.returncode == 0 else 0
        except Exception:
            count = 0
    return str(min(count, 99999))  # 5 digits max


def get_build_date() -> str:
    if "BUILD_DATE" in os.environ:
        return os.environ["BUILD_DATE"]
    return datetime.datetime.utcnow().strftime("%Y%m%d")


kind = DYNAMIC_ENVS[env_name]
if kind == "commit_dev":
    version = f"{get_commit_count()}-dev"
else:
    version = get_build_date()

# Remove any CROSSPOINT_VERSION already present (e.g. the fallback in platformio.ini),
# then inject the computed one so the compiler sees only a single definition.
defines = env.get("CPPDEFINES", [])
defines = [d for d in defines if "CROSSPOINT_VERSION" not in str(d)]
env.Replace(CPPDEFINES=defines)
env.Append(CPPDEFINES=[("CROSSPOINT_VERSION", f'\\"{version}\\"')])

print(f">> gen_version [{env_name}]: CROSSPOINT_VERSION={version}")
