
# Add projects
add_subdirectory("./memory")
add_subdirectory("./allocator")
add_subdirectory("./stack-analysis")
add_subdirectory("./threads")

# Add tests projects
if(ENABLE_TESTS)
add_subdirectory("./memory/test")
add_subdirectory("./allocator/test")
add_subdirectory("./stack-analysis/test")
add_subdirectory("./threads/test")
endif()
