cmake_minimum_required(VERSION 3.14)
project(DistributedStorage C CXX)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(MY_DEBUG)
endif()

# ====== 全局设置 ======
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ====== 开启SIMD编译 ======
execute_process(
    COMMAND bash -c "grep -q avx2 /proc/cpuinfo && echo 1 || echo 0"
    OUTPUT_VARIABLE HAS_AVX2 OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(HAS_AVX2 EQUAL 1)
    add_compile_definitions(VEC256)
    add_compile_options(-mavx2)
else()
    add_compile_definitions(VEC128)
    add_compile_options(-msse4.1)
endif()

# ========== gdb调试 =============
# 1. 设置构建类型为 Debug（或确保默认包含调试信息）
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# 2. 显式添加调试标志（非必需，Debug 模式默认带 -g，但可加强保障）
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_C_FLAGS_DEBUG "-g -O0")

# 3. （可选）禁止剥离符号
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--no-strip")


# ====== 设置可执行文件输出目录 ======
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/exec)

# 查找基础依赖
find_package(Threads REQUIRED)

# # ====== 手动查找 ISA-L 库（兼容 libisal.so）======
find_path(ISAL_INCLUDE_DIR isa-l.h
    PATHS /usr/include /usr/local/include
    REQUIRED
)
find_library(ISAL_LIBRARY NAMES isal
    PATHS /usr/lib /usr/lib64 /usr/local/lib
    REQUIRED
)
message(STATUS "Found ISA-L: include=${ISAL_INCLUDE_DIR}, lib=${ISAL_LIBRARY}")

if(NOT HAS_AVX2 EQUAL 1)
    message(WARNING "AVX2 not detected, ISA-L performance may be suboptimal.")
endif()

# ====== 路径定义 ======
set(YLT_ROOT         ${CMAKE_SOURCE_DIR}/yalantinglibs)
set(JERASURE_ROOT    ${CMAKE_SOURCE_DIR}/Jerasure-1.2A)
set(TINYXML2_ROOT    ${CMAKE_SOURCE_DIR}/tinyxml2-9.0.0)

# ====== 全局编译选项（抑制第三方警告）======
include_directories(SYSTEM ${YLT_ROOT}/include)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(
        -Wno-unused-parameter
        -Wno-implicit-fallthrough
        -Wno-unused-but-set-variable
    )
endif()

# ====== Jerasure 静态库（C）======
add_library(jerasure STATIC
    ${JERASURE_ROOT}/jerasure.c
    ${JERASURE_ROOT}/cauchy.c
    ${JERASURE_ROOT}/galois.c
    ${JERASURE_ROOT}/reed_sol.c
)
target_include_directories(jerasure PUBLIC ${JERASURE_ROOT})
target_compile_options(jerasure PRIVATE
    $<$<C_COMPILER_ID:GNU>:-Wno-maybe-uninitialized -Wno-unused-variable>
    $<$<C_COMPILER_ID:Clang>:-Wno-uninitialized -Wno-unused-variable>
)

# ====== TinyXML2 静态库（C++）======
add_library(tinyxml2 STATIC ${TINYXML2_ROOT}/tinyxml2.cpp)
target_include_directories(tinyxml2 PUBLIC ${TINYXML2_ROOT})
target_compile_options(tinyxml2 PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wno-unused-parameter -Wno-missing-field-initializers>
    $<$<CXX_COMPILER_ID:Clang>:-Wno-unused-parameter -Wno-missing-field-initializers>
)

# ====== 公共工具库：common ======
file(GLOB COMMON_SOURCES "src/common/*.cpp")
add_library(common STATIC ${COMMON_SOURCES})
target_include_directories(common PUBLIC ${CMAKE_SOURCE_DIR}/include/common)
target_link_libraries(common PRIVATE Threads::Threads m)

# ====== ec 库（依赖 common + ISA-L + jerasure）======
file(GLOB EC_SOURCES "src/ec/*.cpp")
add_library(ec STATIC ${EC_SOURCES})
target_include_directories(ec PUBLIC
    ${CMAKE_SOURCE_DIR}/include/ec
    ${JERASURE_ROOT}
    ${ISAL_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/include/common
)
target_link_libraries(ec PRIVATE
    jerasure
    Threads::Threads
    m
    ${ISAL_LIBRARY}
    common
)

# ====== algorithm 库（依赖 ec + common）======
file(GLOB ALGORITHM_SOURCES "src/algorithm/*.cpp")
add_library(algorithm STATIC ${ALGORITHM_SOURCES})
target_include_directories(algorithm PUBLIC
    ${CMAKE_SOURCE_DIR}/include/algorithm
    ${CMAKE_SOURCE_DIR}/include/ec
    ${CMAKE_SOURCE_DIR}/include/common
)
target_link_libraries(algorithm PRIVATE
    ec
    common
    # jerasure 已由 ec 间接依赖，无需显式加
)

# ====== 可执行文件 ======
# Helper: 统一设置 target 属性
function(add_storage_executable TARGET_NAME)
    file(GLOB SOURCES "src/${TARGET_NAME}/*.cpp")
    add_executable(${TARGET_NAME} ${SOURCES})

    target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/include/${TARGET_NAME}
        ${CMAKE_SOURCE_DIR}/include 
        ${YLT_ROOT}/include
        ${YLT_ROOT}/include/ylt/thirdparty
    )

    # 默认链接 algorithm（含 ec + common）
    target_link_libraries(${TARGET_NAME} PRIVATE algorithm)

    target_compile_options(${TARGET_NAME} PRIVATE
        -O2 -Wall -Wextra
        $<$<COMPILE_LANGUAGE:CXX>:-pthread -fcoroutines>
    )
endfunction()

# —— Client ——
add_storage_executable(client)

# —— Coordinator（额外依赖 tinyxml2）——
add_storage_executable(coordinator)
target_link_libraries(coordinator PRIVATE tinyxml2)

# —— Datanode（保持原有静态链接）——
add_storage_executable(datanode)
target_link_libraries(datanode PRIVATE -static-libstdc++ -static-libgcc)

# —— test_client ——
add_storage_executable(test_client)
target_link_libraries(test_client PRIVATE -static-libstdc++ -static-libgcc)

# —— test_server ——
add_storage_executable(test_server)
target_link_libraries(test_server PRIVATE -static-libstdc++ -static-libgcc)