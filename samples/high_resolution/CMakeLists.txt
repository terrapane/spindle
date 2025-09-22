add_executable(high_resolution high_resolution.cpp)

target_link_libraries(high_resolution spindle)

set_target_properties(high_resolution
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)

# On Windows, we must disable the global max() function
if(WIN32)
    target_compile_definitions(high_resolution PRIVATE NOMINMAX)
endif()

target_compile_options(high_resolution
    PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:-Wpedantic -Wextra -Wall>
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>)
