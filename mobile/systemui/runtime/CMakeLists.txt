add_library(fahim_systemui_runtime
    src/FramebufferDisplay.cpp
    src/InputDevice.cpp
    src/InputRouter.cpp
    src/SessionGate.cpp
    src/Compositor.cpp
    src/CredentialBackend.cpp
    src/TeeCredentialBackend.cpp
    src/KernelSessionControl.cpp
    src/LockScreenRenderer.cpp
    src/LockScreenController.cpp
    src/SystemShellRenderer.cpp
    src/SystemShellController.cpp
)

target_include_directories(fahim_systemui_runtime
    PUBLIC include ../lockscreen/native ../tee/include
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

option(FAHIM_ENABLE_OPTEE_CLIENT "Enable the GlobalPlatform OP-TEE client transport" OFF)
if(FAHIM_ENABLE_OPTEE_CLIENT)
    find_package(Threads REQUIRED)
    find_path(FAHIM_TEEC_INCLUDE_DIR tee_client_api.h)
    find_library(FAHIM_TEEC_LIBRARY NAMES teec)
    if(NOT FAHIM_TEEC_INCLUDE_DIR OR NOT FAHIM_TEEC_LIBRARY)
        message(FATAL_ERROR "FAHIM_ENABLE_OPTEE_CLIENT requires tee_client_api.h and libteec")
    endif()
    target_include_directories(fahim_systemui_runtime PRIVATE ${FAHIM_TEEC_INCLUDE_DIR})
    target_compile_definitions(fahim_systemui_runtime PRIVATE FAHIM_ENABLE_OPTEE_CLIENT=1)
    target_link_libraries(fahim_systemui_runtime PUBLIC ${FAHIM_TEEC_LIBRARY} Threads::Threads)
endif()


add_executable(native_lockscreen_snapshot tools/RenderNativeLockScreen.cpp)
target_compile_features(native_lockscreen_snapshot PRIVATE cxx_std_17)
target_compile_options(native_lockscreen_snapshot PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(native_lockscreen_snapshot PRIVATE fahim_systemui_runtime fahim_lockscreen_core)


add_executable(system_shell_tests tests/SystemShellTests.cpp)
target_include_directories(system_shell_tests PRIVATE ../lockscreen/native ../tee/include)
target_compile_features(system_shell_tests PRIVATE cxx_std_17)
target_compile_options(system_shell_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(system_shell_tests PRIVATE fahim_systemui_runtime fahim_lockscreen_core)
add_test(NAME system_shell_tests COMMAND system_shell_tests)
