add_library(fahim_systemui_runtime
    src/FramebufferDisplay.cpp
    src/InputDevice.cpp
    src/InputRouter.cpp
    src/SessionGate.cpp
    src/Compositor.cpp
    src/CredentialBackend.cpp
    src/KernelSessionControl.cpp
    src/LockScreenRenderer.cpp
    src/LockScreenController.cpp
)

target_include_directories(fahim_systemui_runtime
    PUBLIC include ../lockscreen/native
)
target_compile_features(fahim_systemui_runtime PUBLIC cxx_std_17)
target_compile_options(fahim_systemui_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(fahim_systemui_runtime PUBLIC fahim_lockscreen_core)

add_executable(systemui_runtime_tests tests/RuntimeTests.cpp)
target_include_directories(systemui_runtime_tests PRIVATE ../lockscreen/native)
target_compile_features(systemui_runtime_tests PRIVATE cxx_std_17)
target_compile_options(systemui_runtime_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(systemui_runtime_tests PRIVATE fahim_systemui_runtime fahim_lockscreen_core)

add_test(NAME systemui_runtime_tests COMMAND systemui_runtime_tests)
