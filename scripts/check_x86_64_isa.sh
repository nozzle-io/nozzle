#!/usr/bin/env bash
set -euo pipefail

# Check x86_64 ELF objects, archives or executables for AVX/EVEX-class code
# and x86-64-v3 integer extensions, i.e. the instructions that break the
# x86-64-v2 baseline in practice.
#
# Distributed x86_64 Linux binaries target x86-64-v2 (nozzle-dev#196).
# x86-64-v2 allows SSE3, SSSE3, SSE4.1, SSE4.2, POPCNT and CMPXCHG16B. This
# script reports:
#   - any VEX/EVEX-encoded instruction (AVX, AVX2, FMA, F16C, AVX-512), i.e.
#     mnemonics starting with "v", except the VMX/SVM virtualization
#     instructions that also start with "v";
#   - any use of %ymm, %zmm or AVX-512 mask (%k0-%k7) registers;
#   - BMI1/BMI2/LZCNT/MOVBE instructions (x86-64-v3 integer extensions).
#     tzcnt is deliberately not reported: compilers emit "rep bsf", which
#     objdump prints as tzcnt, as a baseline-compatible encoding.
# Not detected: AES-NI, PCLMUL, SHA, ADX, RDRAND, XSAVE, SSE4a, AMX and APX.
# Undecodable "(bad)" lines are counted and reported as a warning, because an
# older objdump prints newer encodings that way (data in .text can also
# produce them).
#
# Only the given files are scanned, not shared libraries they load at
# runtime. Code that legitimately uses newer instructions behind a runtime
# CPUID check can be exempted per function with --allow-symbol REGEX; every
# such entry must be reviewed.
#
# Usage: check_x86_64_isa.sh [--allow-symbol REGEX]... FILE...
# Exit status: 0 = no findings, 1 = findings, 2 = usage or tool error
# (including a file in which no instructions could be parsed, so the check
# cannot pass vacuously).

objdump_bin="${OBJDUMP:-objdump}"
allow_regex=""
files=()

while [ "$#" -gt 0 ]; do
    case "$1" in
        --allow-symbol)
            if [ "$#" -lt 2 ]; then
                echo "error: --allow-symbol needs a regular expression" >&2
                exit 2
            fi
            if [ -z "$allow_regex" ]; then
                allow_regex="$2"
            else
                allow_regex="${allow_regex}|$2"
            fi
            shift 2
            ;;
        -h|--help)
            sed -n '4,32p' "$0"
            exit 0
            ;;
        --)
            shift
            files+=("$@")
            break
            ;;
        -*)
            echo "error: unknown option $1" >&2
            exit 2
            ;;
        *)
            files+=("$1")
            shift
            ;;
    esac
done

if [ "${#files[@]}" -eq 0 ]; then
    echo "usage: $0 [--allow-symbol REGEX]... FILE..." >&2
    exit 2
fi

if ! command -v "$objdump_bin" >/dev/null 2>&1; then
    echo "error: $objdump_bin not found (set OBJDUMP to override)" >&2
    exit 2
fi

status=0
for file in "${files[@]}"; do
    if [ ! -f "$file" ]; then
        echo "error: $file does not exist" >&2
        exit 2
    fi

    # Do not hide objdump failures: a file objdump cannot read is an error.
    if ! disassembly="$("$objdump_bin" -d -C --no-show-raw-insn -w "$file")"; then
        echo "error: $objdump_bin failed on $file" >&2
        exit 2
    fi

    # Pass the regex through the environment: awk -v would process backslash
    # escapes and change its meaning.
    result="$(printf '%s\n' "$disassembly" | NOZZLE_ISA_ALLOW="$allow_regex" awk '
        BEGIN {
            allow = ENVIRON["NOZZLE_ISA_ALLOW"]
            split("vmcall vmlaunch vmresume vmxoff vmxon vmread vmwrite vmptrld vmptrst vmclear vmfunc vmrun vmload vmsave vmmcall verr verw", list, " ")
            for (i in list) virt[list[i]] = 1
            split("andn bextr blsi blsmsk blsr bzhi mulx pdep pext rorx sarx shlx shrx lzcnt movbe", list, " ")
            for (i in list) v3int[list[i]] = 1
            split("lock rep repz repnz repe repne notrack bnd data16 addr32 cs ds ss es fs gs", list, " ")
            for (i in list) prefix[list[i]] = 1
            fn = "?"
            scanned = 0
            found = 0
            undecodable = 0
        }
        /^[0-9a-f]+ <.*>:[[:space:]]*$/ {
            fn = $0
            sub(/^[0-9a-f]+ </, "", fn)
            sub(/>:[[:space:]]*$/, "", fn)
            next
        }
        /^[[:space:]]*[0-9a-f]+:[[:space:]]/ {
            line = $0
            sub(/^[[:space:]]*[0-9a-f]+:[[:space:]]*/, "", line)
            n = split(line, tok, /[[:space:]]+/)
            k = 1
            while (k <= n && (tok[k] in prefix || tok[k] ~ /^rex/ || tok[k] ~ /^\{/)) k++
            if (k > n || tok[k] == "") next
            mnemonic = tok[k]
            if (mnemonic == "(bad)") { undecodable++; next }
            if (mnemonic == "...") next
            scanned++
            bad = 0
            if (mnemonic ~ /^v/ && !(mnemonic in virt)) bad = 1
            else if (mnemonic in v3int) bad = 1
            else if (line ~ /%[yz]mm[0-9]/ || line ~ /%k[0-7]([^0-9]|$)/) bad = 1
            if (!bad) next
            if (allow != "" && fn ~ allow) next
            found++
            if (found <= 20) printf "  %s: %s\n", fn, line
            next
        }
        END {
            printf "SUMMARY %d %d %d\n", scanned, found, undecodable
        }
    ')"

    summary="$(printf '%s\n' "$result" | tail -n 1)"
    details="$(printf '%s\n' "$result" | sed '$d')"
    scanned="$(printf '%s\n' "$summary" | awk '{print $2}')"
    found="$(printf '%s\n' "$summary" | awk '{print $3}')"
    undecodable="$(printf '%s\n' "$summary" | awk '{print $4}')"

    if [ "$scanned" -eq 0 ]; then
        echo "error: no instructions parsed from $file; refusing to report a vacuous pass" >&2
        exit 2
    fi

    if [ "$undecodable" -gt 0 ]; then
        echo "WARN $file: $undecodable undecodable instruction(s) reported as (bad); newer encodings may be unchecked"
    fi

    if [ "$found" -eq 0 ]; then
        echo "OK   $file: $scanned instructions, no AVX/EVEX-class or v3 integer instructions found"
    else
        echo "FAIL $file: $found of $scanned instructions are AVX/EVEX-class or v3 integer instructions (first 20 shown)"
        printf '%s\n' "$details"
        status=1
    fi
done

exit "$status"
