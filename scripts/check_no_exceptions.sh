#!/usr/bin/env bash
set -euo pipefail

# Check that nozzle core code does not use throw/catch for control flow.
# Third-party code (libs/) is excluded.

ROOT="${1:-.}"
EXCLUDE="libs/"
PATTERN='\bthrow\b[[:space:]]|catch[[:space:]]*(\(|\.\.\.)|std::runtime_error|std::logic_error'

hits=0

while IFS= read -r -d '' f; do
    # skip third-party libs
    case "$f" in *"$EXCLUDE"*) continue ;; esac
    # find matches
    if grep -nP "$PATTERN" "$f" 2>/dev/null; then
        hits=$((hits + 1))
    fi
done < <(find "$ROOT/include" "$ROOT/src" -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.mm' \) -print0 2>/dev/null)

if [ "$hits" -gt 0 ]; then
    echo ""
    echo "ERROR: found $hits file(s) with exception usage in nozzle core."
    echo "nozzle APIs must report errors through Result<T> / NozzleErrorCode, not exceptions."
    exit 1
fi

echo "OK: no exception usage found in nozzle core."
exit 0
