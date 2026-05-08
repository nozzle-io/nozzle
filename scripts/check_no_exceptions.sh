#!/usr/bin/env bash
set -euo pipefail

# Check that nozzle core code does not THROW exceptions.
# Internal try/catch is ALLOWED — it catches platform API exceptions and
# converts them to Result<T> errors.  Only `throw` (and convenience aliases
# like std::runtime_error used as throw expressions) are forbidden.
#
# Third-party code (libs/) is excluded.

ROOT="${1:-.}"
EXCLUDE="libs/"
# Detect: throw keyword (as a statement), rethrow, throw-with-new
# Explicitly does NOT flag: catch, std::system_error, std::bad_alloc
# (those are used internally to catch and convert platform exceptions).
PATTERN='\bthrow\b[[:space:]]|\bthrow;|std::runtime_error|std::logic_error'

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
    echo "ERROR: found $hits file(s) with throw usage in nozzle core."
    echo "nozzle APIs must report errors through Result<T> / NozzleErrorCode, not throw."
    echo "Internal catch is allowed (catching platform exceptions to convert to Error)."
    exit 1
fi

echo "OK: no throw usage found in nozzle core."
exit 0
