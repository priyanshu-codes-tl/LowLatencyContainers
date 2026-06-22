cmake_minimum_required(VERSION 3.15)
project(LowLatencyContainers VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Tell CMake where to look for header file inclusions
include_directories(include)

# Compile both your main code and your class logic together
add_executable(LowLatencyContainers
    src/main.cpp
)