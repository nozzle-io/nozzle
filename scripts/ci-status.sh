#!/usr/bin/env bash
# ci-status.sh — Print CI status for all nozzle-io repositories
set -euo pipefail

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
  .github
)

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m'

pass=0
fail=0
other=0

for repo in "${REPOS[@]}"; do
  full="nozzle-io/${repo}"

  status=$(gh run list --repo "$full" --limit 1 --json conclusion -q '.[0].conclusion' 2>/dev/null || echo "not_found")
  sha=$(gh run list --repo "$full" --limit 1 --json headSha -q '.[0].headSha[0:7]' 2>/dev/null || echo "???????")
  time=$(gh run list --repo "$full" --limit 1 --json createdAt -q '.[0].createdAt' 2>/dev/null || echo "")

  case "$status" in
    success)  color=$GREEN;  label="PASS"; ((pass++)) ;;
    failure)  color=$RED;    label="FAIL"; ((fail++)) ;;
    *)        color=$YELLOW; label=$(echo "$status" | tr '[:lower:]' '[:upper:]'); ((other++)) ;;
  esac

  printf "${CYAN}%-20s${NC}  ${color}%-12s${NC}  %s  %s\n" "$repo" "$label" "$sha" "$time"
done

echo ""
total=$((pass + fail + other))
printf "Total: %d repos  ${GREEN}%d pass${NC}  ${RED}%d fail${NC}  ${YELLOW}%d other${NC}\n" "$total" "$pass" "$fail" "$other"

if [ "$fail" -gt 0 ]; then
  exit 1
fi
