#!/usr/bin/env bash
# Current/legacy read-only view; upstream history remains in git.
set -euo pipefail
exec python3 "$(dirname -- "$0")/pbc_view.py" history "$@"
