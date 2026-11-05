#!/usr/bin/env bash
set -euo pipefail

# Check sources/*.txt, the single source list that CMake and non-CMake wrappers
# read. Every list must be well formed and wired into CMake and sources.mk, and
# every compiled file under src/ must appear in exactly one list.
#
# Run from anywhere. Exit codes: 0 consistent, 1 findings, 2 cannot run.

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

groups=(common macos windows linux opengl)
findings=0

report() {
	echo "FAIL $1"
	findings=$((findings + 1))
}

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# A new list that is not wired in everywhere would silently miss some builds.
find sources -type f -name '*.txt' | LC_ALL=C sort > "$work/list_files"
printf 'sources/%s.txt\n' "${groups[@]}" | LC_ALL=C sort > "$work/known_files"
LC_ALL=C comm -23 "$work/list_files" "$work/known_files" > "$work/unknown_files"
while IFS= read -r unknown; do
	report "$unknown is not a known list; add it to this script, CMakeLists.txt and sources.mk"
done < "$work/unknown_files"
for group in "${groups[@]}"; do
	if ! grep -q "^[[:space:]]*nozzle_add_source_group(${group})" CMakeLists.txt; then
		report "CMakeLists.txt does not add the ${group} list"
	fi
	if ! grep -q "nozzle_source_group,${group})" sources/sources.mk; then
		report "sources/sources.mk does not read the ${group} list"
	fi
done

: > "$work/listed"
for group in "${groups[@]}"; do
	list="sources/${group}.txt"
	if [ ! -f "$list" ]; then
		echo "ERROR missing $list" >&2
		exit 2
	fi
	if [ ! -s "$list" ]; then
		report "$list is empty"
		continue
	fi
	if LC_ALL=C grep -q "$(printf '\r')" "$list"; then
		report "$list has CR line endings"
	fi
	if [ -n "$(tail -c 1 "$list")" ]; then
		report "$list has no trailing newline"
	fi
	while IFS= read -r entry || [ -n "$entry" ]; do
		entry="${entry%$'\r'}"
		case "$entry" in
			*/./* | */../* | *//*)
				report "$list: '$entry' is not a normalized path"
				continue
				;;
			src/*.cpp | src/*.mm) ;;
			*)
				report "$list: bad entry '$entry' (one src/ path per line, no comments or blanks)"
				continue
				;;
		esac
		if [ ! -f "$entry" ]; then
			report "$list: $entry does not exist"
		fi
		printf '%s\n' "$entry" >> "$work/listed"
	done < "$list"
done

LC_ALL=C sort "$work/listed" > "$work/listed_sorted"
LC_ALL=C uniq -d "$work/listed_sorted" > "$work/duplicates"
while IFS= read -r entry; do
	report "$entry is listed more than once"
done < "$work/duplicates"

find src -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' -o -name '*.c++' \
	-o -name '*.m' -o -name '*.mm' -o -name '*.s' -o -name '*.S' -o -name '*.asm' \) > "$work/found"
LC_ALL=C sort "$work/found" > "$work/on_disk"
if [ ! -s "$work/on_disk" ]; then
	echo "ERROR no sources found under src/" >&2
	exit 2
fi
LC_ALL=C sort -u "$work/listed_sorted" > "$work/listed_unique"
LC_ALL=C comm -13 "$work/listed_unique" "$work/on_disk" > "$work/unlisted"
while IFS= read -r entry; do
	report "$entry is under src/ but in no list"
done < "$work/unlisted"

if [ "$findings" -ne 0 ]; then
	echo "source manifest: $findings finding(s)"
	exit 1
fi
echo "OK   source manifest: $(wc -l < "$work/on_disk" | tr -d ' ') sources in ${#groups[@]} lists"
