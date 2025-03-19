cmake_minimum_required(VERSION 3.15)
project(asio_practice CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 设置 Debug 编译选项
set(CMAKE_BUILD_TYPE Debug)
add_compile_options(-g)  # 添加调试信息
add_compile_options(-O0) # 关闭优化

# 添加协程支持
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-fcoroutines)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-fcoroutines-ts)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_compile_options(/await)
endif()

find_package(Boost REQUIRED)
find_package(GTest REQUIRED)
add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} 
    PRIVATE 
    Boost::boost
    GTest::gtest 
    GTest::gtest_main
    pthread
) 