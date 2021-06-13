set(target libsat)

# Create library
add_packaged_shared_library(${target} SHARED main.cpp API "SAT_API" LIBS "sat.threads" "sat.memory" "sat.heaps" "sat.stack-analysis")
set_target_properties(${target} PROPERTIES FOLDER "Shared")

# Setting for development comfort
if(DEVMODE AND SATMEM_POST_BUILD_DIRS)
set_target_postbuild_copy(${target} TO ${SATMEM_POST_BUILD_DIRS})
endif()
