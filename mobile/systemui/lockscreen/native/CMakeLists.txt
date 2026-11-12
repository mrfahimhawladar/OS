cmake_minimum_required(VERSION 3.16)
project(FahimLockScreenCore LANGUAGES CXX)

add_library(fahim_lockscreen_core LockScreenCore.cpp)
target_include_directories(fahim_lockscreen_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(fahim_lockscreen_core PUBLIC cxx_std_17)
target_compile_options(fahim_lockscreen_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(lockscreen_core_tests LockScreenCoreTests.cpp)
target_link_libraries(lockscreen_core_tests PRIVATE fahim_lockscreen_core)
target_compile_options(lockscreen_core_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)

enable_testing()
add_test(NAME lockscreen_core_tests COMMAND lockscreen_core_tests)
