cmake_minimum_required(VERSION 3.14)
project(DistributedStorage C CXX)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(MY_DEBUG)  # CMake 3.12+
    # 或旧版：add_definitions(-DDEBUG)
endif()


# ====== 全局设置 ======
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # 禁用 GNU 扩展，更标准
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG ON)
# ? 关键：生成 compile_commands.json（解决 VSCode 头文件跳转问题）
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ====== 设置可执行文件输出目录 ======
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/exec)

# 查找基础依赖
find_package(Threads REQUIRED)


# set(ASIO_ROOT ${CMAKE_SOURCE_DIR}/asio-1.36.0)
set(YLT_ROOT  ${CMAKE_SOURCE_DIR}/yalantinglibs)
set(JERASURE_ROOT ${CMAKE_SOURCE_DIR}/Jerasure-1.2A)

# ====== 全局编译选项（抑制第三方警告）======
include_directories(SYSTEM ${YLT_ROOT}/include)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(
        -Wno-unused-parameter
        -Wno-implicit-fallthrough
        -Wno-unused-but-set-variable 
        # 可选：再加 -Wno-shadow 等常见第三方警告
    )
endif()


# ====== Jerasure 静态库（C）======
add_library(jerasure STATIC
    ${JERASURE_ROOT}/jerasure.c
    ${JERASURE_ROOT}/cauchy.c
    ${JERASURE_ROOT}/galois.c
    ${JERASURE_ROOT}/reed_sol.c
)

target_include_directories(jerasure PUBLIC
    ${JERASURE_ROOT}
    ${CMAKE_SOURCE_DIR}/include  # 让 jerasure 也能 include "utils.h" 等
)

# 关闭jerasure的警告
target_compile_options(jerasure PRIVATE
    $<$<C_COMPILER_ID:GNU>:-Wno-maybe-uninitialized -Wno-unused-variable>
    $<$<C_COMPILER_ID:Clang>:-Wno-uninitialized -Wno-unused-variable>
)


# ====== 定义公共库（如 utils）======
# ====== 公共库：algorithm（含多个 cpp）======
file(GLOB ALGORITHM_SOURCES
    "src/algorithm/*.cpp"
)

# 若需递归子目录（如 src/algorithm/io/*.cpp）：
# file(GLOB_RECURSE ALGORITHM_SOURCES "src/algorithm/*.cpp")

add_library(algorithm STATIC ${ALGORITHM_SOURCES})

# 公共头文件路径：所有模块都能 #include "algorithm/utils.h"
target_include_directories(algorithm PUBLIC
    ${CMAKE_SOURCE_DIR}/include/algorithm
    ${CMAKE_SOURCE_DIR}/Jerasure-1.2A
)

# 公共库自身依赖
target_link_libraries(algorithm PRIVATE
    jerasure
    Threads::Threads
    m
)

# 可选：关闭 algorithm 自身的警告（若需要）
# target_compile_options(algorithm PRIVATE
#     -Wall -Wextra
# )

# ====== 公共库：ec（含多个 cpp）======
file(GLOB EC_SOURCES
    "src/ec/*.cpp"
)

# 若需递归子目录（如 src/ec/io/*.cpp）：
# file(GLOB_RECURSE EC_SOURCES "src/ec/*.cpp")

add_library(ec STATIC ${EC_SOURCES})

# 公共头文件路径：所有模块都能 #include "ec/utils.h"
target_include_directories(ec PUBLIC
    ${CMAKE_SOURCE_DIR}/include/ec
    ${CMAKE_SOURCE_DIR}/include/algorithm
    ${CMAKE_SOURCE_DIR}/Jerasure-1.2A
)

# 公共库自身依赖
target_link_libraries(ec PRIVATE
    algorithm
    Threads::Threads
    m
)


# ====== 为每个模块创建可执行文件 ======

# —— Client 模块 ——
file(GLOB CLIENT_SOURCES "src/client/*.cpp")
add_executable(client ${CLIENT_SOURCES})

target_include_directories(client PRIVATE
    ${CMAKE_SOURCE_DIR}/include/client
    ${CMAKE_SOURCE_DIR}/include/ec
    ${YLT_ROOT}/include
    ${YLT_ROOT}/include/ylt/thirdparty
)

target_link_libraries(client PRIVATE jerasure Threads::Threads m ec)

# —— Coordinator 模块 ——
file(GLOB COORDINATOR_SOURCES "src/coordinator/*.cpp")
add_executable(coordinator ${COORDINATOR_SOURCES})

target_include_directories(coordinator PRIVATE
    ${CMAKE_SOURCE_DIR}/include/coordinator
    ${CMAKE_SOURCE_DIR}/include/ec
    ${CMAKE_SOURCE_DIR}/include/algorithm
    ${YLT_ROOT}/include
    ${YLT_ROOT}/include/ylt/thirdparty
)

target_link_libraries(coordinator PRIVATE jerasure Threads::Threads m ec)

# —— Datanode 模块 ——
file(GLOB DATANODE_SOURCES "src/datanode/*.cpp")
add_executable(datanode ${DATANODE_SOURCES})

target_include_directories(datanode PRIVATE
    ${CMAKE_SOURCE_DIR}/include/datanode
    ${CMAKE_SOURCE_DIR}/include/ec
    ${YLT_ROOT}/include
    ${YLT_ROOT}/include/ylt/thirdparty
)

target_link_libraries(datanode PRIVATE jerasure Threads::Threads m ec)

# —— Proxy 模块 ——
file(GLOB PROXY_SOURCES "src/proxy/*.cpp")
add_executable(proxy ${PROXY_SOURCES})

target_include_directories(proxy PRIVATE
    ${CMAKE_SOURCE_DIR}/include/proxy
    ${CMAKE_SOURCE_DIR}/include/ec
    ${YLT_ROOT}/include
    ${YLT_ROOT}/include/ylt/thirdparty
)

target_link_libraries(proxy PRIVATE jerasure Threads::Threads m ec)


# —— test 模块 ——
file(GLOB TEST_SOURCES "src/test/*.cpp")
add_executable(test ${TEST_SOURCES})

target_include_directories(test PRIVATE
    ${CMAKE_SOURCE_DIR}/include/ec
    ${CMAKE_SOURCE_DIR}/include/algorithm
    ${YLT_ROOT}/include
    ${YLT_ROOT}/include/ylt/thirdparty
)

target_link_libraries(test PRIVATE jerasure Threads::Threads m ec)


# ====== 编译选项（协程等）======
foreach(TARGET client coordinator datanode proxy test)
    target_compile_options(${TARGET} PRIVATE
        -O2 -Wall -Wextra
        $<$<COMPILE_LANGUAGE:CXX>:-pthread>
        $<$<COMPILE_LANGUAGE:CXX>:-fcoroutines>
    )
endforeach()