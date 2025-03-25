cmake_minimum_required(VERSION 3.10)
project(cacheline_benchmark)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加Google Benchmark
find_package(benchmark REQUIRED)

# 添加可执行文件
add_executable(cacheline_benchmark main.cc)

# 链接Google Benchmark
target_link_libraries(cacheline_benchmark benchmark::benchmark)
