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

find_program(CLANG_TIDY_EXE clang-tidy)
if(CLANG_TIDY_EXE)
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
    message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
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

# 当前仓库中可直接构建的服务端模块
add_executable(Car_Air
    source/server/CarModules.cpp
)
target_compile_definitions(Car_Air PRIVATE BUILD_Car_Air)

add_executable(Car_Door
    source/server/CarModules.cpp
)
target_compile_definitions(Car_Door PRIVATE BUILD_Car_Door)

add_executable(Car_Status
    source/server/CarModules.cpp
)
target_compile_definitions(Car_Status PRIVATE BUILD_Car_Status)

add_executable(Car_Fault
    source/server/CarModules.cpp
)
target_compile_definitions(Car_Fault PRIVATE BUILD_Car_Fault)

# AI 大脑进程
add_executable(car_ai
    source/client/car_ai.cpp
)

# ========== nlohmann/json ==========
include(FetchContent)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)
target_link_libraries(car_ai PRIVATE nlohmann_json::nlohmann_json)

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

add_executable(car_tests
    tests/test_CarData.cpp
)
target_link_libraries(car_tests PRIVATE GTest::gtest GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(car_tests)
