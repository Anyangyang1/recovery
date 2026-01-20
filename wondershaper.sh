#!/bin/bash

# 配置参数
NODES=("node3" "node4" "node5" "node6" "node7" "node8" "node11" "node12" "node14" "node15" "node17" "node18")  # 替换为你的节点主机名或IP
INTERFACE="ens9"                 # 网卡名称
DOWNLOAD_KBPS=15000000            # 下行带宽（Kbps）
UPLOAD_KBPS=15000000              # 上行带宽（Kbps）
WONDERSHAPER_PATH="~/wondershaper/wondershaper"

# 对每个节点执行限速
for node in "${NODES[@]}"; do
    echo "Applying bandwidth limit on $node..."
    ssh "$node" "sudo $WONDERSHAPER_PATH -c -a $INTERFACE 2>/dev/null; sudo $WONDERSHAPER_PATH -a $INTERFACE -d $DOWNLOAD_KBPS -u $UPLOAD_KBPS"
done

echo "Done."