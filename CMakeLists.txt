cmake_minimum_required(VERSION 3.10)
project(xraw3d_unit_test LANGUAGES CXX)
set(TARGET_PROJECT "xraw3d_unit_test")
set(COMPONENT_REGISTRY "")

# Fetch Common.cmake from xcmake_tools
include(FetchContent)
FetchContent_Declare(
  xcmake_tools
  GIT_REPOSITORY https://github.com/LIONant-depot/xcmake_tools.git
  GIT_TAG main
  GIT_SHALLOW TRUE
  SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/xcmake_tools"
)
FetchContent_MakeAvailable(xcmake_tools)
include(${CMAKE_BINARY_DIR}/_deps/xcmake_tools/Common.cmake)

# Define the main executable
add_executable(${TARGET_PROJECT}
  "source/unit_test/main.cpp"
)

# Organize source files in IDE
source_group("example/generate_documentation" FILES
)

source_group("" FILES
  "source/unit_test/main.cpp"
)

# Add dependency subdirectory
add_subdirectory("build/dependency" "${CMAKE_CURRENT_BINARY_DIR}/xraw3d")

# Process components
ProcessComponents()