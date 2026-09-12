#!/usr/bin/env bash
# Root-level wrapper -- the real implementation lives in scripts/run_fsoc.sh
# (repo convention: shell utilities live under scripts/, e.g.
# scripts/run_baseline_demo.sh). Kept as a one-liner so `./run_fsoc.sh` at
# the repo root is the only command anyone needs to remember.
set -Eeuo pipefail
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/scripts/run_fsoc.sh" "$@"
