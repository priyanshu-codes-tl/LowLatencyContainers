cmake_minimum_required(VERSION 3.15)
project(LowLatencyContainers VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# 2. Inject Aggressive Low-Latency Optimization Flags
# -O3 turns on maximum vectorization and optimization loops
# -march=native optimizes the machine instructions
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -Wall -Wextra")

# Tell CMake where to look for header file inclusions
include_directories(include)

# Compile both your main code and your class logic together
add_executable(LowLatencyContainers
    src/main.cpp
)

# 4. Find and Link Native Linux Threading Libraries (-lpthread)
find_package(Threads REQUIRED)
target_link_libraries(LowLatencyContainers PRIVATE Threads::Threads)