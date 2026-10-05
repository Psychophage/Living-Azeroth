#!/usr/bin/env bash
# Backup current and legacy PBC tables; upstream tool history remains in git.
# This is a character-system backup. Deployment also needs the full realm DBs/config.
set -euo pipefail
umask 077
pbc_database="${PBC_DATABASE:-acore_characters}"
[[ "$pbc_database" =~ ^[A-Za-z0-9_]+$ ]] || { echo 'Invalid database name.' >&2; exit 1; }
options=()
if [[ -n "${PBC_MYSQL_DEFAULTS_FILE:-}" ]]; then
  options+=("--defaults-extra-file=$PBC_MYSQL_DEFAULTS_FILE")
fi
output="${1:-pbc_backup_$(date -u +%Y%m%dT%H%M%SZ).sql}"
[[ ! -e "$output" ]] || { echo 'Output already exists; refusing to overwrite it.' >&2; exit 1; }
mapfile -t tables < <(mysql "${options[@]}" --batch --skip-column-names "$pbc_database" -e   "SELECT table_name FROM information_schema.tables WHERE table_schema=DATABASE() AND (LEFT(table_name,4)='pbc_' OR LEFT(table_name,8)='mod_pbc_') ORDER BY table_name")
[[ "${#tables[@]}" -gt 0 ]] || { echo 'No PBC tables found; no backup written.' >&2; exit 1; }
temporary=$(mktemp "${output}.tmp-XXXXXX")
trap 'rm -f -- "$temporary"' EXIT
mysqldump "${options[@]}" --single-transaction --no-tablespaces "$pbc_database" "${tables[@]}" > "$temporary"
# Link creates the final name atomically and fails if another writer created it.
ln -- "$temporary" "$output"
echo "Character-system backup saved: $output (${#tables[@]} current/legacy tables)."
