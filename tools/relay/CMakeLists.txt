cmake_minimum_required(VERSION 3.20)
project(hlds-relay LANGUAGES CXX)

# The relay that consoles playing online meet through. POSIX sockets (Linux,
# macOS, BSD); the core has none, and is what the tests run.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# Everything built with AddressSanitizer and UndefinedBehaviorSanitizer: what
# CI runs the tests with, so that a bad read of a datagram stops the test.
option(HLDS_RELAY_SANITIZE "Build with ASan and UBSan" OFF)
if(HLDS_RELAY_SANITIZE AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

add_library(hlds_relay_core STATIC core/Relay.cpp core/Serve.cpp core/Limits.cpp)
target_include_directories(hlds_relay_core PUBLIC
    core
    "${REPO_ROOT}/shared"      # the relay's datagrams and the game's frames, as the DS has them
    "${REPO_ROOT}/thirdparty"
)
# (The shared headers' byte order, pinned as the ARM9 build and the converter
# pin it.)
target_compile_definitions(hlds_relay_core PUBLIC _LITTLE_ENDIAN)
if(NOT MSVC)
    target_compile_options(hlds_relay_core PRIVATE -Wall -Wextra)
endif()

add_executable(hlds-relay main.cpp)
target_link_libraries(hlds-relay PRIVATE hlds_relay_core)

# Rooms of make-believe consoles playing through a relay over UDP: how much
# it keeps up with (loadtest/loadtest.cpp; README's "Load test").
add_executable(hlds-relay-loadtest loadtest/loadtest.cpp)
target_include_directories(hlds-relay-loadtest PRIVATE "${REPO_ROOT}/shared" "${REPO_ROOT}/thirdparty")
target_compile_definitions(hlds-relay-loadtest PRIVATE _LITTLE_ENDIAN)

# The relay under libFuzzer, for long runs by hand (clang only; not a ctest):
#   CXX=clang++ cmake -S tools/relay -B build/relay-fuzz -DHLDS_RELAY_LIBFUZZER=ON
#   cmake --build build/relay-fuzz --target relay_libfuzzer
#   build/relay-fuzz/relay_libfuzzer -max_total_time=600
option(HLDS_RELAY_LIBFUZZER "Build the libFuzzer target (clang)" OFF)
if(HLDS_RELAY_LIBFUZZER)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "HLDS_RELAY_LIBFUZZER needs clang (CXX=clang++)")
    endif()
    add_library(hlds_relay_core_fuzz STATIC core/Relay.cpp core/Serve.cpp core/Limits.cpp)
    target_include_directories(hlds_relay_core_fuzz PUBLIC core "${REPO_ROOT}/shared" "${REPO_ROOT}/thirdparty")
    target_compile_definitions(hlds_relay_core_fuzz PUBLIC _LITTLE_ENDIAN)
    target_compile_options(hlds_relay_core_fuzz PUBLIC -fsanitize=fuzzer-no-link,address,undefined)
    add_executable(relay_libfuzzer tests/fuzz_relay_libfuzzer.cpp)
    target_link_libraries(relay_libfuzzer PRIVATE hlds_relay_core_fuzz)
    target_compile_options(relay_libfuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(relay_libfuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
endif()

option(HLDS_BUILD_TESTS "Build the relay's tests" ON)
if(HLDS_BUILD_TESTS)
    enable_testing()
    foreach(test_name test_relay fuzz_relay)
        add_executable(${test_name} tests/${test_name}.cpp)
        target_link_libraries(${test_name} PRIVATE hlds_relay_core)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
    # (A short one, the relay started by the test itself: that it serves two
    # full rooms over real UDP, a map being sent in one go, with little lost.)
    add_test(NAME relay_loadtest_smoke
             COMMAND hlds-relay-loadtest --spawn $<TARGET_FILE:hlds-relay> --rooms 2 --players 8 --seconds 3 --map
                     --most-loss 2)
endif()
