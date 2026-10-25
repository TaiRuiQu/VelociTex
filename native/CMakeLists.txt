cmake_minimum_required(VERSION 3.20)
project(VelociTexCore VERSION 1.0.0 LANGUAGES CXX)

add_library(velocitex_core STATIC
    core/types.cpp core/lexer.cpp core/macros.cpp core/parser.cpp core/cache.cpp
    core/layout.cpp core/geometry.cpp core/mathml.cpp core/svg.cpp core/core.cpp
    core/generated/data.cpp core/generated/svg_data.cpp
    core/html/domtree.cpp core/html/options.cpp core/html/build_common.cpp
    core/html/build_html.cpp core/html/svg.cpp core/html/delimiter.cpp
    core/html/accent.cpp core/html/array.cpp core/html/enclose.cpp
    core/generated/html_paths.cpp core/generated/html_sqrt_paths.cpp
    core/pool_alloc.cpp)
target_compile_features(velocitex_core PUBLIC cxx_std_20)
target_include_directories(velocitex_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
set_target_properties(velocitex_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(VelociTex.Core.Native SHARED core/api.cpp)
target_link_libraries(VelociTex.Core.Native PRIVATE velocitex_core)
if(MSVC)
    set_property(TARGET velocitex_core VelociTex.Core.Native PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(velocitex_core PRIVATE /utf-8 /W4 /fp:precise)
    target_compile_options(VelociTex.Core.Native PRIVATE /utf-8 /W4 /fp:precise)
else()
    target_compile_options(velocitex_core PRIVATE -Wall -Wextra)
    target_compile_options(VelociTex.Core.Native PRIVATE -Wall -Wextra)
endif()
