
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
set_target_properties(mimalloc-static PROPERTIES FOLDER "Tools")
set_property(TARGET mimalloc-static PROPERTY POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(mimalloc-static PRIVATE ${mi_defines} MI_STATIC_LIB)
target_compile_options(mimalloc-static PRIVATE ${mi_cflags})
target_link_libraries(mimalloc-static PUBLIC ${mi_libraries})
target_include_directories(mimalloc-static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${mi_install_incdir}>
)
