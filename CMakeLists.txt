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

# ========== MySQL ==========
find_library(MYSQL_LIBRARY mysqlclient)
if(MYSQL_LIBRARY)
    message(STATUS "MySQL client found: ${MYSQL_LIBRARY}")
else()
    message(WARNING "libmysqlclient not found. Install: sudo apt install libmysqlclient-dev")
endif()

# ========== hiredis ==========
include(FetchContent)
FetchContent_Declare(
    hiredis
    GIT_REPOSITORY https://github.com/redis/hiredis.git
    GIT_TAG v1.2.0
)
FetchContent_MakeAvailable(hiredis)

# ========== libcurl ==========
find_package(CURL QUIET)

# ========== nlohmann/json ==========
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# 中控主程序
add_executable(car_main
    source/main.cpp
    source/Car_Log.cpp
    source/ConfigManager.cpp
    source/AsyncAuditLogger.cpp
    source/RedisManager.cpp
)

target_link_libraries(car_main PRIVATE nlohmann_json::nlohmann_json)
if(MYSQL_LIBRARY)
    target_link_libraries(car_main PRIVATE ${MYSQL_LIBRARY})
    target_compile_definitions(car_main PRIVATE HAS_MYSQL=1)
endif()
target_link_libraries(car_main PRIVATE hiredis)

# 客户端工具
add_executable(car_ctl
    source/client/car_ctl.cpp
)

# 服务端子模块（每个模块独立源文件，不再依赖编译宏区分）
add_executable(Car_Air    source/server/modules/air.cpp)
add_executable(Car_Door   source/server/modules/door.cpp)
add_executable(Car_Status source/server/modules/status.cpp)
add_executable(Car_Fault  source/server/modules/fault.cpp)

# AI 大脑进程（依赖 libcurl）
if(CURL_FOUND)
    add_executable(car_ai
        source/client/car_ai.cpp
    )
    target_link_libraries(car_ai PRIVATE nlohmann_json::nlohmann_json CURL::libcurl)
else()
    message(WARNING "libcurl not found — car_ai will not be built. Install libcurl4-openssl-dev")
endif()

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

# ========== 新模块测试 ==========
add_executable(test_async_audit_logger
    tests/test_AsyncAuditLogger.cpp
    source/AsyncAuditLogger.cpp
)
target_link_libraries(test_async_audit_logger PRIVATE GTest::gtest GTest::gtest_main)
if(MYSQL_LIBRARY)
    target_link_libraries(test_async_audit_logger PRIVATE ${MYSQL_LIBRARY})
    target_compile_definitions(test_async_audit_logger PRIVATE HAS_MYSQL=1)
endif()
target_include_directories(test_async_audit_logger PRIVATE ${CMAKE_SOURCE_DIR}/include)
gtest_discover_tests(test_async_audit_logger)

add_executable(test_redis_manager
    tests/test_RedisManager.cpp
    source/RedisManager.cpp
)
target_link_libraries(test_redis_manager PRIVATE GTest::gtest GTest::gtest_main nlohmann_json::nlohmann_json hiredis)
target_include_directories(test_redis_manager PRIVATE ${CMAKE_SOURCE_DIR}/include)
gtest_discover_tests(test_redis_manager)
