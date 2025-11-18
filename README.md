# 库的安装

## asio

+ 下载最新版本的asio库

```bash
wget https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-30-2.tar.gz
```

+ 解压

```bash
tar -zxvf asio-1-30-2.tar.gz
```

解压后得到的目录

```bash
asio-1.30.2/               ← 注意：目录名是 asio-1.30.2（不是 asio-1-30-2！）
└── include/
    ├── asio.hpp
    └── asio/              ← 所有头文件在此
```

+ 编写makefile文件，进行编译

## C++17

```bash
# 查看当前版本
g++ --version

# 安装新版本
sudo yum install centos-release-scl
sudo yum install devtoolset-9-gcc devtoolset-9-gcc-c++


## 2选1，临时启用或永久生效
scl enable devtoolset-9 bash  # 临时启用 GCC 9
# 或加到 ~/.bashrc：
# echo "source /opt/rh/devtoolset-9/enable" >> ~/.bashrc


echo "source /opt/rh/devtoolset-9/enable" >> ~/.bashrc
source ~/.bashrc   # 立即生效当前终端，永久生效
```

## 编译器识别到老版本的asio

直接编辑 `.vscode/c_cpp_properties.json`

```json
{
    "configurations": [
        {
            "name": "Linux",
            "includePath": [
                "${workspaceFolder}/asio-1.30.2/include",   // ← 必须放最前面！
                "${workspaceFolder}/include",
                "${workspaceFolder}/Jerasure-1.2A",
                "/usr/include",        // 系统路径放后面
                "/usr/local/include"
            ],
            "defines": [],
            "compilerPath": "/opt/rh/devtoolset-9/root/usr/bin/g++",  // 可选：指定新 GCC
            "cStandard": "c11",
            "cppStandard": "c++17",   // ? 必须和 Makefile 一致
            "intelliSenseMode": "linux-gcc-x64"
        }
    ],
    "version": 4
}
```
## coro_rpc安装
+ 安装ylt
```bash
# 正确克隆（含所有子模块）
git clone --recursive git@github.com:alibaba/yalantinglibs.git  
```

# 工程构建

## 首次构建
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```
`-j`：并行编译（加速构建）
`$(nproc)`：shell 命令替换，展开为 CPU 核心数

## 运行
```bash
./main
```

## 清理
```bash
make clean
# 或 rm -rf *
```

