add_executable(test_thread_pool test_thread_pool.cpp)

target_link_libraries(test_thread_pool spindle Terra::stf)

add_test(NAME test_thread_pool
         COMMAND test_thread_pool)

set_target_properties(test_thread_pool
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)

target_compile_options(test_thread_pool PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
    $<$<CXX_COMPILER_ID:MSVC>: >)
