set(target sat.memory)

# Collect source files
append_group_sources(files FILTER "*.cc|*.cpp|*.h|*.hpp" ROOT "./src" DIRECTORIES 
  "./"
  # Win32 specific
  "./win32"
)
append_group_sources(files FILTER "*.h|*.hpp" GROUP "Include" ROOT "${CMAKE_SOURCE_DIR}/include" DIRECTORIES "./sat/memory")

# Create library
add_library(${target} STATIC ${files})
set_target_properties(${target} PROPERTIES FOLDER "Libs")

# Set compile properties
target_include_directories(${target} PUBLIC "${CMAKE_SOURCE_DIR}/include")
target_compile_definitions(${target} PRIVATE "SAT_API=__declspec(dllexport)")

# Set link properties
target_link_libraries(${target} PRIVATE sat.threads)
