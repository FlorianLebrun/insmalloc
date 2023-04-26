
set(mi_sources
    src/stats.cpp
    src/random.cpp
    src/os.cpp
    src/bitmap.cpp
    src/arena.cpp
    src/region.cpp
    src/segment.cpp
    src/page.cpp
    src/alloc.cpp
    src/alloc-aligned.cpp
    src/alloc-posix.cpp
    src/heap.cpp
    src/options.cpp
    src/init.cpp)

add_library(mimalloc-static STATIC ${mi_sources})
set_property(TARGET mimalloc-static PROPERTY POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(mimalloc-static PRIVATE MI_STATIC_LIB)
target_compile_options(mimalloc-static PRIVATE $<$<CONFIG:Release>:/O2 /Ob2 /Oi /Ot /Oy /GT /GL /GS- /guard:cf>)
target_include_directories(mimalloc-static PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

