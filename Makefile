CXX       := g++
CC        := gcc
CXXFLAGS  := -std=c++11 -O2 -Wall -Wextra -I./Jerasure-1.2A -I./include
CFLAGS    := -O2 -Wall -Wextra -I./Jerasure-1.2A
BUILD_DIR := build
SRC_DIR   := src

# Jerasure 的 C 源文件（必须全部包含）
JERASURE_SRC := Jerasure-1.2A/jerasure.c \
                Jerasure-1.2A/cauchy.c \
                Jerasure-1.2A/galois.c \
                Jerasure-1.2A/reed_sol.c

# 编译成 .o 文件
JERASURE_OBJS := $(JERASURE_SRC:Jerasure-1.2A/%.c=$(BUILD_DIR)/%.o)

# 你的 C++ 源文件
MAIN_OBJ := $(BUILD_DIR)/main.o
TARGET   := $(BUILD_DIR)/main

.PHONY: all clean

all: $(TARGET)

# 最终链接：C++ 主程序 + Jerasure 的 .o + 数学库
$(TARGET): $(MAIN_OBJ) $(JERASURE_OBJS) | $(BUILD_DIR)
	$(CXX) $^ -o $@ -lm

# 编译 main.cpp（C++）
$(BUILD_DIR)/main.o: $(SRC_DIR)/main.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 编译 Jerasure 的 C 文件
$(BUILD_DIR)/%.o: Jerasure-1.2A/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)