cmake_minimum_required(VERSION 3.15)
project(asio_practice CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
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