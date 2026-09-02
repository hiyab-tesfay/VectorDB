cmake_minimum_required(VERSION 3.20)
project(minivdb LANGUAGES CXX)

add_library(minivdb_lib src/minivdb.cpp)
target_include_directories(minivdb_lib PUBLIC include)
target_compile_features(minivdb_lib PUBLIC cxx_std_20)
target_compile_options(minivdb_lib PRIVATE -Wall -Wextra -Wpedantic)

add_executable(minivdb src/main.cpp)
target_link_libraries(minivdb PRIVATE minivdb_lib)
add_executable(minivdb_bench bench/benchmark.cpp)
target_link_libraries(minivdb_bench PRIVATE minivdb_lib)

include(CTest)
add_executable(minivdb_tests tests/test.cpp)
target_link_libraries(minivdb_tests PRIVATE minivdb_lib)
add_test(NAME minivdb_tests COMMAND minivdb_tests)

