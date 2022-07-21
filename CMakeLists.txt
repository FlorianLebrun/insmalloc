cmake_minimum_required(VERSION 3.19)
include(./cmake-toolkit.cmake)

project(${PROJECT_PACKAGE_NAME}
LANGUAGES ASM_MASM C CXX CSharp
  VERSION ${PROJECT_PACKAGE_VERSION}
)

# Add own targets
add_subdirectory(./thirdparty/mimalloc)
add_subdirectory(./node_modules/AVL-Tree/lib)
add_subdirectory(./lib/ins.memory.hooks)
#add_subdirectory(./lib/ins.memory.space)
add_subdirectory(./lib/ins.memory.space2)
#add_subdirectory(./test)
add_subdirectory(./test2)
