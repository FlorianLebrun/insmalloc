cmake_minimum_required(VERSION 3.19)
include(./cmake-toolkit.cmake)

project(${PROJECT_PACKAGE_NAME}
LANGUAGES ASM_MASM C CXX CSharp
  VERSION ${PROJECT_PACKAGE_VERSION}
)

# Add own targets
add_subdirectory(./thirdparty/mimalloc)
add_subdirectory(./lib/sat.memory.space)
add_subdirectory(./lib/sat.memory.hooks)
add_subdirectory(./test)
