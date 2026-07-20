add_executable(test_spinlock test_spinlock.cpp)

target_link_libraries(test_spinlock spindle Terra::stf)

add_test(NAME test_spinlock
         COMMAND test_spinlock)

set_target_properties(test_spinlock
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)

target_compile_options(test_spinlock
    PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:-Wpedantic -Wextra -Wall -Wconversion -Wshadow>
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>)
