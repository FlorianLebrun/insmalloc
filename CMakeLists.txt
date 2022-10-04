cmake_minimum_required(VERSION 3.19)
include(./cmake-toolkit.cmake)

project(${PROJECT_PACKAGE_NAME}
LANGUAGES ASM_MASM C CXX CSharp
  VERSION ${PROJECT_PACKAGE_VERSION}
)

# Add own targets
add_subdirectory(./node_modules/AVL-Tree/lib)
add_subdirectory(./lib/ins.memory.hooks)
add_subdirectory(./lib/ins.memory.space)
add_subdirectory(./tools/ins.devtools)
add_subdirectory(./tools/mimalloc)
add_subdirectory(./test)

set_target_properties(ins.avl PROPERTIES FOLDER "Libs")
set_target_properties(ins.memory.hooks PROPERTIES FOLDER "Libs")
set_target_properties(ins.memory.space PROPERTIES FOLDER "Libs")
