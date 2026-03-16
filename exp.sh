#!/bin/bash

# ================== 配置区 ==================
COORDINATOR_HOST="node2"
CLIENT_HOST="node1"
DATANODES=("node3" "node4" "node5" "node6" "node7" "node8" "node11" "node12" "node14" "node15" "node17" "node18")


# 参数数组（可自定义）
k_vals=(8)
m_vals=(3)
w_vals=(4)
stripe_num=1000

INTERFACE="ens9"  # 网卡名，请根据实际修改（如 ens192）

# 限速列表（单位：Gbps → 转为 kbps）
# 格式: (label, kbps_down, kbps_up)
throttles=(
    "10G 10000000 10000000"
    "5G  5000000 5000000"
    "2G  2000000 2000000"
    "1G  1000000 1000000"
    "0.5G 500000 500000"
)

LOG_DIR="./repair_logs"
mkdir -p "$LOG_DIR"

# ================== 辅助函数 ==================

run_fg() {
    local host=$1; shift
    ssh "$host" "$*"
}

run_bg() {
    local host=$1; shift
    ssh "$host" "nohup $* > /dev/null 2>&1 &"
}

kill_procs() {
    for host in "${DATANODES[@]}"; do
        ssh "$host" "pkill -f datanode" 2>/dev/null || true
    done
    ssh "$COORDINATOR_HOST" "pkill -f coordinator" 2>/dev/null || true
    ssh "$CLIENT_HOST" "pkill -f client" 2>/dev/null || true
}

# cleanup_wondershaper() {
#     for host in "${DATANODES[@]}"; do
#         ssh "$host" "sudo ./exp/wondershaper -c -a $INTERFACE" 2>/dev/null || true
#     done
# }

# apply_throttle() {
#     local down_kbps=$1
#     local up_kbps=$2
#     for host in "${DATANODES[@]}"; do
#         ssh "$host" "sudo ./exp/wondershaper -a $INTERFACE -d $down_kbps -u $up_kbps"
#     done
# }
cleanup_wondershaper() {
    local FULL_PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/sbin:/usr/bin:/bin"
    for host in "${DATANODES[@]}"; do
        ssh "$host" "sudo env PATH='$FULL_PATH' /home/anyangyang/exp/wondershaper -c -a $INTERFACE" 2>/dev/null || true
    done
}

apply_throttle() {
    local down_kbps=$1
    local up_kbps=$2
    local FULL_PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/sbin:/usr/bin:/bin"
    for host in "${DATANODES[@]}"; do
        ssh "$host" "sudo env PATH='$FULL_PATH' /home/anyangyang/exp/wondershaper -a $INTERFACE -d $down_kbps -u $up_kbps"
    done
}

# ================== 主逻辑 ==================

# 外层：遍历参数组合
for k in "${k_vals[@]}"; do
    for m in "${m_vals[@]}"; do
        for w in "${w_vals[@]}"; do
            b=8

            echo ">>> Running experiment: k=$k, m=$m, w=$w, b=$b"

            # 启动 datanodes
            echo "  Starting datanodes..."
            for host in "${DATANODES[@]}"; do
                run_bg "$host" "./exp/datanode"
            done
            sleep 8

            # 启动 coordinator
            echo "  Starting coordinator on $COORDINATOR_HOST..."
            run_bg "$COORDINATOR_HOST" "./exp/coordinator $k $m $w $b"
            sleep 8

            # client set
            echo "  Running client set..."
            run_fg "$CLIENT_HOST" "./exp/client set $k $b $stripe_num" >/dev/null

            # 遍历限速场景
            for throttle in "${throttles[@]}"; do
                read label down_kbps up_kbps <<< "$throttle"
                echo "    Applying throttling: $label"

                # 设置限速
                cleanup_wondershaper
                apply_throttle "$down_kbps" "$up_kbps"
                sleep 2

                # 定义日志文件
                log_file="$LOG_DIR/k${k}_m${m}_w${w}_b${b}_${label}.log"
                echo "      Logging to: $log_file"
                echo "=== Experiment: k=$k, m=$m, w=$w, b=$b, bandwidth=$label ===" > "$log_file"
                echo "Start time: $(date)" >> "$log_file"
                echo "----------------------------------------" >> "$log_file"

                # 执行 repair 命令并记录
                for cmd in \
                    "repair_node_no_local 0" \
                    "repair_node 0" \
                    "repair_node_opt 0" \
                    "repair_node_no_local_con 0" \
                    "repair_node_con 0" \
                    "repair_node_opt_con 0"; do

                    echo "      Running: ./exp/client $cmd"
                    echo ">>> Command: ./exp/client $cmd" >> "$log_file"
                    echo "Time: $(date)" >> "$log_file"
                    output=$(run_fg "$CLIENT_HOST" "./exp/client $cmd" 2>&1)
                    echo "$output" >> "$log_file"
                    echo "----------------------------------------" >> "$log_file"
                    sleep 1
                done

                echo "      Done with $label"
            done

            # 清理本轮
            echo "  Cleaning up processes and throttling..."
            kill_procs
            cleanup_wondershaper
            sleep 5
        done
    done
done

echo "All experiments completed. Logs in $LOG_DIR"
