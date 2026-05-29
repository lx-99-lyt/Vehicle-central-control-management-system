cmake_minimum_required(VERSION 3.17)

project(Car_Device_System VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wshadow -Wdouble-promotion
    -Wformat=2 -Wunused
    -Wold-style-cast
    -Werror
    -O2
)

option(ENABLE_CLANG_TIDY "Enable clang-tidy static analysis (slow, CI only)" OFF)

find_program(CLANG_TIDY_EXE clang-tidy)
if(CLANG_TIDY_EXE AND ENABLE_CLANG_TIDY)
    set(CMAKE_CXX_CLANG_TIDY
        "${CLANG_TIDY_EXE}"
        "-checks=bugprone-*,performance-*,modernize-*,misc-*,\
-readability-*,cppcoreguidelines-*,\
-modernize-use-trailing-return-type,\
-readability-magic-numbers,-cppcoreguidelines-avoid-magic-numbers,\
-modernize-avoid-c-arrays,-cppcoreguidelines-avoid-c-arrays,\
-cppcoreguidelines-pro-bounds-array-to-pointer-decay,\
-hicpp-avoid-c-arrays,-hicpp-no-array-decay,\
-readability-identifier-length,\
-cppcoreguidelines-pro-type-union-access,\
-cppcoreguidelines-pro-type-reinterpret-cast,\
-cppcoreguidelines-macro-usage,\
-cppcoreguidelines-avoid-non-const-global-variables,\
-cppcoreguidelines-pro-bounds-pointer-arithmetic,\
-bugprone-easily-swappable-parameters,\
-cppcoreguidelines-pro-type-member-init,\
-cppcoreguidelines-special-member-functions,\
-cppcoreguidelines-pro-type-vararg,\
-cppcoreguidelines-pro-bounds-constant-array-index,\
-modernize-raw-string-literal"
        "--warnings-as-errors=*"
        "-header-filter=${CMAKE_SOURCE_DIR}/include/.*|${CMAKE_SOURCE_DIR}/source/.*"
    )
    message(STATUS "clang-tidy enabled: ${CLANG_TIDY_EXE}")
elseif(CLANG_TIDY_EXE)
    message(STATUS "clang-tidy found but disabled (use -DENABLE_CLANG_TIDY=ON to enable)")
else()
    message(WARNING "clang-tidy not installed. Run: sudo apt install clang-tidy")
endif()

# ========== Sanitizers ==========
option(ENABLE_SANITIZERS "Enable AddressSanitizer + UBSan runtime checks" OFF)
if(ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
    message(STATUS "Sanitizers enabled: AddressSanitizer + UBSan")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include_directories(${CMAKE_SOURCE_DIR}/include)

# 中控主程序
add_executable(car_main
    source/main.cpp
    source/Car_Log.cpp
    source/ConfigManager.cpp
)

# 客户端工具
add_executable(car_ctl
    source/client/car_ctl.cpp
)

# 服务端子模块（每个模块独立源文件，不再依赖编译宏区分）
add_executable(Car_Air    source/server/modules/air.cpp)
add_executable(Car_Door   source/server/modules/door.cpp)
add_executable(Car_Status source/server/modules/status.cpp)
add_executable(Car_Fault  source/server/modules/fault.cpp)

# ========== libcurl ==========

# ========== nlohmann/json ==========
include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# AI 大脑进程
# AI 大脑进程
add_executable(car_ai source/client/car_ai.cpp)

# 直接链接我们刚才编译的极简静态库，彻底告别依赖地狱
target_link_libraries(car_ai PRIVATE 
    nlohmann_json::nlohmann_json 
    /home/lx/curl-8.7.1/lib/.libs/libcurl.a
    dl pthread
)

# ========== GoogleTest ==========
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

target_compile_options(gtest PRIVATE -Wno-double-promotion -Wno-old-style-cast)
target_compile_options(gtest_main PRIVATE -Wno-double-promotion -Wno-old-style-cast)
set_target_properties(gtest gtest_main gmock gmock_main PROPERTIES CXX_CLANG_TIDY "")

enable_testing()

# ========== 测试目标 ==========

# CarData 基础测试
add_executable(test_CarData tests/test_CarData.cpp)
target_link_libraries(test_CarData PRIVATE GTest::gtest GTest::gtest_main)

# CarData 扩展测试（findField、网络字节序、枚举值等）
add_executable(test_CarDataExtended tests/test_CarDataExtended.cpp)
target_link_libraries(test_CarDataExtended PRIVATE GTest::gtest GTest::gtest_main)

# ConfigManager 测试
add_executable(test_ConfigManager
    tests/test_ConfigManager.cpp
    source/ConfigManager.cpp
    source/Car_Log.cpp
)
target_link_libraries(test_ConfigManager PRIVATE GTest::gtest GTest::gtest_main)

# Logger 测试
add_executable(test_Logger
    tests/test_Logger.cpp
    source/Car_Log.cpp
)
target_link_libraries(test_Logger PRIVATE GTest::gtest GTest::gtest_main)

# Door 模块测试
add_executable(test_DoorModule tests/test_DoorModule.cpp)
target_link_libraries(test_DoorModule PRIVATE GTest::gtest GTest::gtest_main)

# Air 模块测试
add_executable(test_AirModule tests/test_AirModule.cpp)
target_link_libraries(test_AirModule PRIVATE GTest::gtest GTest::gtest_main)

# Status 模块测试
add_executable(test_StatusModule tests/test_StatusModule.cpp)
target_link_libraries(test_StatusModule PRIVATE GTest::gtest GTest::gtest_main)

# Fault 模块测试
add_executable(test_FaultModule tests/test_FaultModule.cpp)
target_link_libraries(test_FaultModule PRIVATE GTest::gtest GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(test_CarData)
gtest_discover_tests(test_CarDataExtended)
gtest_discover_tests(test_ConfigManager)
gtest_discover_tests(test_Logger)
gtest_discover_tests(test_DoorModule)
gtest_discover_tests(test_AirModule)
gtest_discover_tests(test_StatusModule)
gtest_discover_tests(test_FaultModule)
