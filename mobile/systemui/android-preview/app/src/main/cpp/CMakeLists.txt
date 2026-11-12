cmake_minimum_required(VERSION 3.22.1)
project(fahim_lockscreen_preview LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(SYSTEMUI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../../" ABSOLUTE)
get_filename_component(RUNTIME_ROOT "${SYSTEMUI_ROOT}/runtime" ABSOLUTE)
get_filename_component(LOCKSCREEN_ROOT "${SYSTEMUI_ROOT}/lockscreen/native" ABSOLUTE)

set(RENDERER_SOURCE "${RUNTIME_ROOT}/src/LockScreenRenderer.cpp")
set(COMPOSITOR_SOURCE "${RUNTIME_ROOT}/src/Compositor.cpp")
set(SESSION_GATE_SOURCE "${RUNTIME_ROOT}/src/SessionGate.cpp")

add_library(fahim_lockscreen_preview SHARED
    NativeBridge.cpp
    ${RENDERER_SOURCE}
    ${COMPOSITOR_SOURCE}
    ${SESSION_GATE_SOURCE}
)

target_include_directories(fahim_lockscreen_preview PRIVATE
    "${RUNTIME_ROOT}/include"
    "${LOCKSCREEN_ROOT}"
)

target_compile_options(fahim_lockscreen_preview PRIVATE
    -Wall -Wextra -Wpedantic
)

find_library(android-log log)
target_link_libraries(fahim_lockscreen_preview PRIVATE ${android-log})
