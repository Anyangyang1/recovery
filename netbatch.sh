#!/bin/bash
# Fixed nodes: 10.0.0.5, 10.0.0.6, 10.0.0.7
# Real-time display + final CSV output

# NODES=("10.0.0.4" "10.0.0.5" "10.0.0.6" "10.0.0.7" "10.0.0.8" "10.0.0.9" "10.0.0.10" "10.0.0.11" "10.0.0.12" "10.0.0.13" "10.0.0.14" "10.0.0.15" "10.0.0.17" "10.0.0.18")
# NODES=("10.0.0.4" "10.0.0.5" "10.0.0.6")
NODES=("100.0.0.3" "100.0.0.4" "100.0.0.5" "100.0.0.6" "100.0.0.7" "100.0.0.8" "100.0.0.10" "100.0.0.11" "100.0.0.12" "100.0.0.13" "100.0.0.14" "100.0.0.16" "100.0.0.17") #9配漏了，没有18
# NODES=("100.0.0.10" "100.0.0.11" "100.0.0.12" "100.0.0.13" "100.0.0.14" "100.0.0.16" "100.0.0.17") #9配漏了，没有18

PORT=5201
TIMEOUT_CMD="timeout 20"

# Initialize result matrix with "-"
printf "%-15s" "src\\dst"
for dst in "${NODES[@]}"; do
    printf " %12s" "$dst"
done
echo
echo "------------------------------------------------------------"

declare -A RESULT

for src in "${NODES[@]}"; do
    printf "%-15s" "$src"
    for dst in "${NODES[@]}"; do
        if [ "$src" = "$dst" ]; then
            RESULT["$src,$dst"]="-"
            printf " %12s" "-"
            continue
        fi

        # Start server on dst
        ssh "$dst" "($TIMEOUT_CMD iperf3 -s -p $PORT -1 >/dev/null 2>&1) &" >/dev/null 2>&1
        sleep 0.3

        # Run client
        out=$($TIMEOUT_CMD ssh "$src" "iperf3 -c $dst -p $PORT -t 5 -f g -Z 2>/dev/null" 2>/dev/null)

        # Parse
        bw=$(echo "$out" | awk '/sender$/ {b=$7} END {print b}')
        if [ -z "$bw" ] || [ "$bw" = "0.00" ] || [ "$bw" = "0" ]; then
            RESULT["$src,$dst"]="FAIL"
        else
            RESULT["$src,$dst"]=$(printf "%.2f" "$bw")
        fi

        printf " %12s" "${RESULT[$src,$dst]}"
        sleep 0.1  # visual pacing
    done
    echo
done

echo
echo "[INFO] Test completed. Saving to bandwidth_matrix.csv ..."

# --- Write final CSV ---
{
    printf "src\\dst"
    for dst in "${NODES[@]}"; do
        printf ",%s" "$dst"
    done
    echo

    for src in "${NODES[@]}"; do
        printf "%s" "$src"
        for dst in "${NODES[@]}"; do
            printf ",%s" "${RESULT[$src,$dst]}"
        done
        echo
    done
} > ./data/bandwidth_matrix_100.csv

echo "[DONE] CSV saved: bandwidth_matrix.csv"

