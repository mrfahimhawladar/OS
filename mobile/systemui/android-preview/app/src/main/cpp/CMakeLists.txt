cmake_minimum_required(VERSION 3.22.1)
project(fahim_lockscreen_preview LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SYSTEMUI_ROOT "\${CMAKE_CURRENT_LIST_DIR}/../../../../../")
set(RUNTIME_ROOT "\${SYSTEMUI_ROOT}/runtime")

add_library(fahim_lockscreen_preview SHARED
    NativeBridge.cpp
    "\${RUNTIME_ROOT}/src/LockScreenRenderer.cpp"
    "\${RUNTIME_ROOT}/src/Compositor.cpp"
    "\${RUNTIME_ROOT}/src/SessionGate.cpp"
)

target_include_directories(fahim_lockscreen_preview PRIVATE
    "\${RUNTIME_ROOT}/include"
    "\${SYSTEMUI_ROOT}/lockscreen/native"
)

target_compile_options(fahim_lockscreen_preview PRIVATE
    -Wall -Wextra -Wpedantic
)

find_library(android-log log)
target_link_libraries(fahim_lockscreen_preview PRIVATE "\${android-log}")
