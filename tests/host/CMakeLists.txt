# Host-side unit tests for the hardware-independent firmware logic.
#
#   cmake -S tests/host -B build-host && cmake --build build-host && ctest --test-dir build-host
#   (or: python3 scripts/host_tests.py)
#
# Only pure modules are compiled here: no ESP-IDF, FreeRTOS or LVGL. If a
# product source starts needing one of those, move that code back behind its
# device-side wrapper instead of adding a stub here.
cmake_minimum_required(VERSION 3.16)
project(sensecap_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(FW_MAIN "${CMAKE_CURRENT_SOURCE_DIR}/../../main")

option(HOST_TESTS_SANITIZE "Build host tests with ASan + UBSan" ON)

# ── Dependencies (pinned) ─────────────────────────────────────────────────────
# Offline builds: pass -DFETCHCONTENT_SOURCE_DIR_UNITY=<path> and
# -DFETCHCONTENT_SOURCE_DIR_CJSON=<path> to use local checkouts.
include(FetchContent)
FetchContent_Declare(unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG        v2.6.1
    GIT_SHALLOW    TRUE
)
# cJSON: ESP-IDF's `json` component wraps upstream cJSON; keep this tag in step
# with the IDF version used for the firmware. SOURCE_SUBDIR points at a path
# without a CMakeLists.txt so only the sources are fetched (we compile cJSON.c
# directly instead of using cJSON's own build).
FetchContent_Declare(cjson
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    GIT_TAG        v1.7.18
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _sources_only
)
FetchContent_MakeAvailable(unity cjson)

add_library(cjson_host STATIC "${cjson_SOURCE_DIR}/cJSON.c")
target_include_directories(cjson_host PUBLIC "${cjson_SOURCE_DIR}")

# ── Code under test ───────────────────────────────────────────────────────────
add_library(fw_pure STATIC
    ${FW_MAIN}/ha/ha_payload.c
    ${FW_MAIN}/sensor/sensor_format.c
    ${FW_MAIN}/rp2040/rp2040_protocol.c
    ${FW_MAIN}/display/display_logic.c
    ${FW_MAIN}/util/indicator_util.c
    ${FW_MAIN}/util/cobs.c
)
target_include_directories(fw_pure PUBLIC
    # stubs/ only provides esp_log.h (no-op logging) for indicator_util.c.
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${FW_MAIN}
    ${FW_MAIN}/ha
    ${FW_MAIN}/sensor
    ${FW_MAIN}/rp2040
    ${FW_MAIN}/display
    ${FW_MAIN}/util
)
target_link_libraries(fw_pure PUBLIC cjson_host)

set(HOST_WARNINGS -Wall -Wextra -Werror)
target_compile_options(fw_pure PRIVATE ${HOST_WARNINGS})

if(HOST_TESTS_SANITIZE)
    set(SAN_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    foreach(t fw_pure cjson_host unity)
        target_compile_options(${t} PRIVATE ${SAN_FLAGS})
    endforeach()
    target_link_options(fw_pure PUBLIC -fsanitize=address,undefined)
endif()

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()

set(HOST_TESTS
    test_ha_payload
    test_sensor_format
    test_rp2040_protocol
    test_display_logic
    test_indicator_util
)

foreach(t IN LISTS HOST_TESTS)
    add_executable(${t} ${t}.c)
    target_link_libraries(${t} PRIVATE fw_pure unity)
    target_compile_options(${t} PRIVATE ${HOST_WARNINGS})
    if(HOST_TESTS_SANITIZE)
        target_compile_options(${t} PRIVATE ${SAN_FLAGS})
    endif()
    add_test(NAME ${t} COMMAND ${t})
endforeach()
