#!/usr/bin/env bash
# Run all 6 algorithms × 10 workloads × 5 budget levels on the SDSC Blue 512-host cluster.
# Must be executed from the repo root inside the nix develop shell.
#
# Usage: bash run_all.sh
#
# Paper: Dutot et al., Towards Energy Budget Control in HPC, CCGrid 2017
#
# Power constants (Table I):
#   P̃_idle = 100 W   (estimated idle, used by scheduler)
#   P̃_comp = 203.12 W (estimated compute, used by scheduler)
#   P_off   = 9.75 W  (switched-off standby)
#   P_idle_actual = 95 W    (measured, used in monitoring correction)
#   P_comp_actual = 190.74 W (measured, used in monitoring correction)
#   Monitoring period = 600 s
#
# Budget window: middle 3 days of the 7-day trace (days 2–5).
# Peak energy = 512 hosts × 190.74 W × 259200 s = 2.531×10¹⁰ J
# 49% ≈ idle-only energy (512 × 95 W × 259200 s), as noted in the paper.
# Budgets: 100, 90, 80, 70, 60, 50, 49, 30 % of peak — matching paper Figure 3.

set -e

WORKLOAD_DIR="assets/workload/sdscblue"
OUT_DIR="out"
PLATFORM="assets/cluster512.xml"

T_S=172800   # day 2
T_E=432000   # day 5

# Budget levels: budget_pct → B in joules (512 × 190.74 × 259200 × pct/100)
declare -A BUDGET_J
BUDGET_J[100]="2.531e10"
BUDGET_J[90]="2.278e10"
BUDGET_J[80]="2.025e10"
BUDGET_J[70]="1.772e10"
BUDGET_J[60]="1.519e10"
BUDGET_J[50]="1.266e10"
BUDGET_J[49]="1.240e10"
BUDGET_J[30]="7.594e9"

BUDGET_PCTS=(100 90 80 70 60 50 49 30)

ALGORITHMS=(
    "pc_idle"
    "energybud_idle"
    "reducepc_idle"
    "pc_shutdown"
    "energybud_shutdown"
    "reducepc_shutdown"
)

WORKLOADS=(
    "sdscblue_week80_delay"
    "sdscblue_week96_delay"
    "sdscblue_week97_delay"
    "sdscblue_week98_delay"
    "sdscblue_week101_delay"
    "sdscblue_week103_delay"
    "sdscblue_week105_delay"
    "sdscblue_week109_delay"
    "sdscblue_week110_delay"
    "sdscblue_week112_delay"
)

# ── Verify build ──────────────────────────────────────────────────────────────

for ALGO in "${ALGORITHMS[@]}"; do
    LIB="./build/lib${ALGO}.so"
    if [ ! -f "$LIB" ]; then
        echo "ERROR: $LIB not found — run ninja -C build first"
        exit 1
    fi
done

# ── Run all algorithms × workloads × budget levels ────────────────────────────

TOTAL=$(( ${#ALGORITHMS[@]} * ${#WORKLOADS[@]} * ${#BUDGET_PCTS[@]} ))
echo "=== Running sdscblue (${#ALGORITHMS[@]} algos × ${#WORKLOADS[@]} workloads × ${#BUDGET_PCTS[@]} budgets, 512 hosts) ==="
echo "    Budget window: t_s=${T_S} s, t_e=${T_E} s"
echo "    Budget levels: ${BUDGET_PCTS[*]}% of peak (2.531×10¹⁰ J)"
echo "    Total runs: $TOTAL"
echo ""

COUNT=0
for PCT in "${BUDGET_PCTS[@]}"; do
    B="${BUDGET_J[$PCT]}"
    INIT_JSON="{\"B\":${B},\"t_s\":${T_S},\"t_e\":${T_E}}"
    INIT_SIZE=${#INIT_JSON}

    for WORKLOAD in "${WORKLOADS[@]}"; do
        WORKLOAD_FILE="$WORKLOAD_DIR/${WORKLOAD}.json"
        if [ ! -f "$WORKLOAD_FILE" ]; then
            echo "WARNING: $WORKLOAD_FILE not found, skipping"
            continue
        fi

        for ALGO in "${ALGORITHMS[@]}"; do
            COUNT=$(( COUNT + 1 ))
            LIB="./build/lib${ALGO}.so"
            OUT_PREFIX="$OUT_DIR/${ALGO}/budget${PCT}/${WORKLOAD}/"

            echo "[$COUNT/$TOTAL] ${PCT}% — $ALGO × $WORKLOAD"
            mkdir -p "$OUT_PREFIX"

            batsim \
                -l "$LIB" "$INIT_SIZE" "$INIT_JSON" \
                -p "$PLATFORM" \
                --mmax 512 \
                --energy-host \
                -w "$WORKLOAD_FILE" \
                -e "$OUT_PREFIX" \
                2>/dev/null
        done
    done
done

# ── Unconstrained baseline (plain EASY backfilling) ───────────────────────────
# Runs the energy-unaware EASY scheduler on all workloads.
# Results stored in out/easy/baseline/<workload>/ for the reference line.

BASELINE_JSON='{"B":0,"t_s":0,"t_e":0}'
BASELINE_SIZE=${#BASELINE_JSON}
BASELINE_LIB="./build/libeasy.so"

if [ ! -f "$BASELINE_LIB" ]; then
    echo "ERROR: $BASELINE_LIB not found — run ninja -C build first"
    exit 1
fi

echo "=== Running unconstrained baseline (easy × ${#WORKLOADS[@]} workloads) ==="
BCOUNT=0
for WORKLOAD in "${WORKLOADS[@]}"; do
    WORKLOAD_FILE="$WORKLOAD_DIR/${WORKLOAD}.json"
    if [ ! -f "$WORKLOAD_FILE" ]; then
        continue
    fi

    BCOUNT=$(( BCOUNT + 1 ))
    OUT_PREFIX="$OUT_DIR/easy/baseline/${WORKLOAD}/"

    echo "[$BCOUNT/${#WORKLOADS[@]}] baseline — easy × $WORKLOAD"
    mkdir -p "$OUT_PREFIX"

    batsim \
        -l "$BASELINE_LIB" "$BASELINE_SIZE" "$BASELINE_JSON" \
        -p "$PLATFORM" \
        --mmax 512 \
        --energy-host \
        -w "$WORKLOAD_FILE" \
        -e "$OUT_PREFIX" \
        2>/dev/null
done

echo ""
echo "=== Done. Results in $OUT_DIR/ ==="
