#!/bin/bash

NODES=("node3" "node4" "node5" "node6" "node7" "node8" "node11" "node12" "node14" "node15" "node17" "node18")
PROGRAM="datanode"

if [[ $# -ne 1 ]] || [[ "$1" != "start" && "$1" != "kill" ]]; then
    echo "Usage: $0 {start|kill}"
    exit 1
fi

ACTION="$1"

for node in "${NODES[@]}"; do
    echo "[$ACTION] on $node..."
    if [[ "$ACTION" == "kill" ]]; then
        # 使用更可靠的匹配：只要命令行包含 /exp/datanode 就杀
        ssh "$node" "pkill -f '$PROGRAM' 2>/dev/null || true"
    else
        ssh "$node" "
            cd exp && nohup ./$PROGRAM > ${PROGRAM}.log 2>&1 &
        "
    fi
done

echo "Done."