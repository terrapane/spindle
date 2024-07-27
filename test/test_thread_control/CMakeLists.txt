add_executable(test_thread_control test_thread_control.cpp)

target_link_libraries(test_thread_control spindle Terra::stf)

add_test(NAME test_thread_control
         COMMAND test_thread_control)

set_target_properties(test_thread_control
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)

target_compile_options(test_thread_control PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
    $<$<CXX_COMPILER_ID:MSVC>: >)
