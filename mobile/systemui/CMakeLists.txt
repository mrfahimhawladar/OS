cmake_minimum_required(VERSION 3.16)
project(FahimSystemUI LANGUAGES CXX)

enable_testing()
add_subdirectory(lockscreen/native)
add_subdirectory(runtime)
