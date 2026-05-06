#!/usr/bin/env bash
# tag-all.sh — Create and push an annotated tag across all nozzle-io repositories
set -euo pipefail

if [ $# -lt 1 ]; then
  echo "usage: tag-all.sh <tag> [message]"
  echo "  tag     — tag name (e.g. v0.0.1-gamma)"
  echo "  message — optional tag message (defaults to tag name)"
  exit 1
fi

TAG="$1"
MESSAGE="${2:-$TAG}"

REPOS=(
  nozzle
  ofxNozzle
  tcxNozzle
  jit.nozzle
  nozzle-TOP
  py.nozzle
  obs-nozzle
  nozzle.swift
  nozzle.rs
  blender-nozzle
  nozzle.unity
  nozzle-sokol
  Nozzle.NET
  .github
)

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

ok=0
skip=0
fail=0

for repo in "${REPOS[@]}"; do
  full="nozzle-io/${repo}"

  existing=$(git ls-remote --tags "https://github.com/${full}.git" "refs/tags/${TAG}" 2>/dev/null | head -1)
  if [ -n "$existing" ]; then
    printf "${CYAN}%-20s${NC}  ${RED}EXISTS${NC}  %s\n" "$repo" "$TAG"
    ((skip++))
    continue
  fi

  sha=$(gh api "repos/${full}/commits/main" --jq '.sha[0:7]' 2>/dev/null || echo "???????")

  if gh api "repos/${full}/git/refs" \
    --method POST \
    -f "ref=refs/tags/${TAG}" \
    -f "sha=$(gh api "repos/${full}/commits/main" --jq '.sha' 2>/dev/null)" \
    > /dev/null 2>&1; then
    printf "${CYAN}%-20s${NC}  ${GREEN}OK${NC}      %s  %s\n" "$repo" "$sha" "$TAG"
    ((ok++))
  else
    printf "${CYAN}%-20s${NC}  ${RED}FAIL${NC}    %s  %s\n" "$repo" "$sha" "$TAG"
    ((fail++))
  fi
done

echo ""
total=$((ok + skip + fail))
printf "Total: %d repos  ${GREEN}%d tagged${NC}  ${YELLOW}%d skipped${NC}  ${RED}%d failed${NC}\n" "$total" "$ok" "$skip" "$fail"

if [ "$fail" -gt 0 ]; then
  exit 1
fi
