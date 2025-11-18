cmake_minimum_required(VERSION 3.14)
project(DistributedStorage C CXX)

# ====== 全局设置 ======
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # 禁用 GNU 扩展，更标准
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG ON)

# ? 关键：生成 compile_commands.json（解决 VSCode 头文件跳转问题）
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 查找基础依赖
find_package(Threads REQUIRED)

# asio
# ylt

# set(ASIO_ROOT ${CMAKE_SOURCE_DIR}/asio-1.36.0)
set(YLT_ROOT  ${CMAKE_SOURCE_DIR}/yalantinglibs)

# # 检查路径是否存在
# if(NOT EXISTS ${ASIO_ROOT}/include/asio.hpp)
#     message(FATAL_ERROR "asio not found at ${ASIO_ROOT}/include/asio.hpp")
# endif()
# if(NOT EXISTS ${YLT_ROOT}/include/ylt/coro_rpc/coro_rpc_server.hpp)
#     message(FATAL_ERROR "ylt not found at ${YLT_ROOT}/include/ylt/coro_rpc/...")
# endif()



# ====== Jerasure 静态库（C）======
add_library(jerasure STATIC
    Jerasure-1.2A/jerasure.c
    Jerasure-1.2A/cauchy.c
    Jerasure-1.2A/galois.c
    Jerasure-1.2A/reed_sol.c
)

target_include_directories(jerasure PUBLIC
    ${CMAKE_SOURCE_DIR}/Jerasure-1.2A
    ${CMAKE_SOURCE_DIR}/include  # 让 jerasure 也能 include "utils.h" 等
)

target_compile_options(jerasure PRIVATE
    -Wall -Wextra -O2
)

# ====== 主程序（C++）======
file(GLOB MAIN_SOURCES "src/*.cpp")  # 自动扫描 src/*.cpp

add_executable(main ${MAIN_SOURCES})

# ? 头文件路径：实现“零路径 include”
target_include_directories(main PRIVATE
    # 你的本地头文件（实现 #include "sggh.h"）
    ${CMAKE_SOURCE_DIR}/include
    
    # asio
    # ${ASIO_ROOT}/include
    
    # ylt 及其依赖（async_simple, struct_pack）
    ${YLT_ROOT}/include
    ${YLT_ROOT}/include/ylt/thirdparty
    # ${YLT_ROOT}/struct_pack/include
)

# 链接依赖
target_link_libraries(main PRIVATE
    jerasure
    Threads::Threads
    m  # libm (math)
)

# ====== 编译选项（与 Makefile 一致）======
target_compile_options(main PRIVATE
    -O2 -Wall -Wextra
    $<$<COMPILE_LANGUAGE:CXX>:-pthread>  # 仅 C++ 加 -pthread
    $<$<COMPILE_LANGUAGE:CXX>:-fcoroutines>  # ← 关键！启用协程
)
