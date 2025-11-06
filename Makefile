# Makefile
CXX       := g++
CC        := gcc
CXXFLAGS  := -std=c++11 -O2 -Wall -Wextra -Iinclude -IJerasure-1.2A
CFLAGS    := -O2 -Wall -Wextra -Iinclude -IJerasure-1.2A
BUILD_DIR := build
SRC_DIR   := src

# Jerasure 的 C 源文件（必须全部编译）
JERASURE_SRC := Jerasure-1.2A/jerasure.c \
                Jerasure-1.2A/cauchy.c \
                Jerasure-1.2A/galois.c \
                Jerasure-1.2A/reed_sol.c

# 你的 C++ 源文件（main.cpp, sggh.cpp, utils.cpp）
CPP_SRC := $(wildcard $(SRC_DIR)/*.cpp)

# 转换为 .o 输出路径
JERASURE_OBJS := $(JERASURE_SRC:Jerasure-1.2A/%.c=$(BUILD_DIR)/%.o)
CPP_OBJS      := $(CPP_SRC:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)

# 最终可执行文件
TARGET := $(BUILD_DIR)/main

.PHONY: all clean

all: $(TARGET)

# 链接所有目标文件 + 数学库
$(TARGET): $(CPP_OBJS) $(JERASURE_OBJS) | $(BUILD_DIR)
	$(CXX) $^ -o $@ -lm

# 编译 C++ 源文件
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 编译 Jerasure 的 C 文件
$(BUILD_DIR)/%.o: Jerasure-1.2A/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# 创建 build 目录
$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)