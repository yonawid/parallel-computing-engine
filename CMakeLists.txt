cmake_minimum_required(VERSION 3.20)
project(parallel_computing_engine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(PCE_ENABLE_TSAN "Build with ThreadSanitizer (GCC/Clang only)" OFF)

find_package(Threads REQUIRED)

add_library(pce_thread_pool STATIC src/thread_pool.cpp)
target_include_directories(pce_thread_pool PUBLIC include)
target_compile_features(pce_thread_pool PUBLIC cxx_std_20)
target_link_libraries(pce_thread_pool PUBLIC Threads::Threads)

function(pce_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    endif()
    if(PCE_ENABLE_TSAN AND NOT MSVC)
        target_compile_options(${target} PRIVATE -fsanitize=thread -g)
        target_link_options(${target} PRIVATE -fsanitize=thread)
    endif()
endfunction()

pce_set_warnings(pce_thread_pool)

add_executable(pce_example examples/main.cpp)
target_link_libraries(pce_example PRIVATE pce_thread_pool)
pce_set_warnings(pce_example)

enable_testing()
add_executable(pce_tests tests/test_thread_pool.cpp)
target_link_libraries(pce_tests PRIVATE pce_thread_pool)
pce_set_warnings(pce_tests)
add_test(NAME thread_pool_tests COMMAND pce_tests)
set_tests_properties(thread_pool_tests PROPERTIES TIMEOUT 120)
