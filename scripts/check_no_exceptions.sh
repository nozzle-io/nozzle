#!/usr/bin/env bash
set -euo pipefail

# Check that nozzle core code does not THROW exceptions.
# Internal try/catch is ALLOWED — it catches platform API exceptions and
# converts them to Result<T> errors.  Only `throw` (and convenience aliases
# like std::runtime_error used as throw expressions) are forbidden.
#
# Third-party code (libs/) is excluded.
#
# Uses grep -nE (POSIX ERE) for macOS/BSD portability.
# grep errors (bad option, missing file) cause script failure.

ROOT="${1:-.}"
EXCLUDE="libs/"

# POSIX ERE patterns: word-boundary workaround with (^|[^a-zA-Z_0-9])
# - throw followed by space or ( :  (^|[^a-zA-Z_0-9])throw[[:space:](]
# - rethrow (throw;) :  throw;
# - std::runtime_error or std::logic_error used as throw expression types
PATTERN='(^|[^a-zA-Z_0-9])throw[[:space:](;]|std::runtime_error|std::logic_error'

hits=0
match_lines=""

while IFS= read -r -d '' f; do
    # skip third-party libs
    case "$f" in *"$EXCLUDE"*) continue ;; esac
    # grep -nE: line-numbered POSIX ERE.  Do NOT suppress stderr.
    found=$(grep -nE "$PATTERN" "$f" || true)
    if [ -n "$found" ]; then
        hits=$((hits + 1))
        # prefix each line with the filename
        while IFS= read -r line; do
            match_lines="${match_lines}${f}:${line}
"
        done <<< "$found"
    fi
done < <(find "$ROOT/include" "$ROOT/src" -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.mm' \) -print0 2>/dev/null)

if [ "$hits" -gt 0 ]; then
    echo ""
    echo "ERROR: found throw usage in nozzle core:"
    echo "$match_lines"
    echo "nozzle APIs must report errors through Result<T> / NozzleErrorCode, not throw."
    echo "Internal catch is allowed (catching platform exceptions to convert to Error)."
    exit 1
fi

echo "OK: no throw usage found in nozzle core."
exit 0
