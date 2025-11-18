# Makefile —— 支持 asio + 本地头文件零路径引用
CXX       := g++
CC        := gcc
CXXFLAGS  := -std=c++17 -O2 -Wall -Wextra -pthread \
             -Iinclude \
             -IJerasure-1.2A \
             -Iasio-1.36.0/include   # ← 新增：asio 头文件路径

CFLAGS    := -O2 -Wall -Wextra -Iinclude -IJerasure-1.2A

BUILD_DIR := build
SRC_DIR   := src

# Jerasure C 源文件
JERASURE_SRC := Jerasure-1.2A/jerasure.c \
                Jerasure-1.2A/cauchy.c \
                Jerasure-1.2A/galois.c \
                Jerasure-1.2A/reed_sol.c

# C++ 源文件
CPP_SRC := $(wildcard $(SRC_DIR)/*.cpp)

JERASURE_OBJS := $(JERASURE_SRC:Jerasure-1.2A/%.c=$(BUILD_DIR)/%.o)
CPP_OBJS      := $(CPP_SRC:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)

TARGET := $(BUILD_DIR)/main

.PHONY: all clean

all: $(TARGET)

# 链接时显式加 -lpthread（更稳妥，尤其 glibc < 2.34）
$(TARGET): $(CPP_OBJS) $(JERASURE_OBJS) | $(BUILD_DIR)
	$(CXX) $^ -o $@ -lpthread -lm

# 编译 C++：使用 CXXFLAGS（含 -pthread, -Iinclude, -Iasio...）
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 编译 C：使用 CFLAGS（不需要 -pthread / asio）
$(BUILD_DIR)/%.o: Jerasure-1.2A/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)