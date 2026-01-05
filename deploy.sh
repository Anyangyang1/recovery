#!/bin/bash

# 配置区（按需修改）
BIN_DIR="./exec"
USER="anyangyang"
REMOTE_DIR="~/exp"

# 节点列表：datanode 部署到 node3~5，client 部署到 node6
DATANODE_NODES=("node3" "node4" "node5" "node6" "node7" "node8" "node11" "node12" "node13" "node14" "node15" "node17" "node18")
# DATANODE_NODES=("node11" "node12" "node13" "node14" "node15" "node17" "node18")
CLIENT_NODES=("node1")

# 检查本地文件是否存在
if [[ ! -f "$BIN_DIR/datanode" ]]; then
    echo "Error: $BIN_DIR/datanode not found"
    exit 1
fi
if [[ ! -f "$BIN_DIR/client" ]]; then
    echo "Error: $BIN_DIR/client not found"
    exit 1
fi
if [[ ! -f "$BIN_DIR/test_client" ]]; then
    echo "Error: $BIN_DIR/client not found"
    exit 1
fi

# 部署 datanode
echo "Deploying datanode to ${DATANODE_NODES[*]}..."
for node in "${DATANODE_NODES[@]}"; do
    echo "$node"
    scp "$BIN_DIR/datanode" "$USER@$node:$REMOTE_DIR/" || {
        echo "Failed to copy to $node"
        exit 1
    }
done

# 部署 client
echo "Deploying client to ${CLIENT_NODES[*]}..."
for node in "${CLIENT_NODES[@]}"; do
    echo "$node"
    scp "$BIN_DIR/client" "$USER@$node:$REMOTE_DIR/" || {
        echo "Failed to copy to $node"
        exit 1
    }
done

# # 部署 test_client
# echo "Deploying test_client to ${DATANODE_NODES[*]}..."
# for node in "${DATANODE_NODES[@]}"; do
#     echo "$node"
#     scp "$BIN_DIR/test_client" "$USER@$node:$REMOTE_DIR/" || {
#         echo "Failed to copy to $node"
#         exit 1
#     }
# done


# # 部署 test_server
# echo "Deploying test_server to ${DATANODE_NODES[*]}..."
# for node in "${DATANODE_NODES[@]}"; do
#     echo "$node"
#     scp "$BIN_DIR/test_server" "$USER@$node:$REMOTE_DIR/" || {
#         echo "Failed to copy to $node"
#         exit 1
#     }
# done

echo "All binaries deployed successfully."