cmake_minimum_required(VERSION 3.20)
project(ai_coder_cli CXX C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find packages provided by vcpkg
find_package(cxxopts CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(unofficial-tree-sitter CONFIG REQUIRED)

# Include FetchContent for grammars
include(FetchContent)

# Fetch tree-sitter-cpp
FetchContent_Declare(
    tree_sitter_cpp
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-cpp.git
    GIT_TAG        v0.22.3
)
FetchContent_GetProperties(tree_sitter_cpp)
if(NOT tree_sitter_cpp_POPULATED)
    FetchContent_Populate(tree_sitter_cpp)
endif()

set(TS_CPP_SOURCES ${tree_sitter_cpp_SOURCE_DIR}/src/parser.c)
if(EXISTS ${tree_sitter_cpp_SOURCE_DIR}/src/scanner.cc)
    list(APPEND TS_CPP_SOURCES ${tree_sitter_cpp_SOURCE_DIR}/src/scanner.cc)
elseif(EXISTS ${tree_sitter_cpp_SOURCE_DIR}/src/scanner.c)
    list(APPEND TS_CPP_SOURCES ${tree_sitter_cpp_SOURCE_DIR}/src/scanner.c)
endif()

add_library(tree-sitter-cpp STATIC ${TS_CPP_SOURCES})
target_include_directories(tree-sitter-cpp PUBLIC ${tree_sitter_cpp_SOURCE_DIR}/src)
target_link_libraries(tree-sitter-cpp PRIVATE unofficial::tree-sitter::tree-sitter)

# Fetch tree-sitter-python
FetchContent_Declare(
    tree_sitter_python
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-python.git
    GIT_TAG        v0.21.0
)
FetchContent_GetProperties(tree_sitter_python)
if(NOT tree_sitter_python_POPULATED)
    FetchContent_Populate(tree_sitter_python)
endif()

set(TS_PYTHON_SOURCES ${tree_sitter_python_SOURCE_DIR}/src/parser.c)
if(EXISTS ${tree_sitter_python_SOURCE_DIR}/src/scanner.cc)
    list(APPEND TS_PYTHON_SOURCES ${tree_sitter_python_SOURCE_DIR}/src/scanner.cc)
elseif(EXISTS ${tree_sitter_python_SOURCE_DIR}/src/scanner.c)
    list(APPEND TS_PYTHON_SOURCES ${tree_sitter_python_SOURCE_DIR}/src/scanner.c)
endif()

add_library(tree-sitter-python STATIC ${TS_PYTHON_SOURCES})
target_include_directories(tree-sitter-python PUBLIC ${tree_sitter_python_SOURCE_DIR}/src)
target_link_libraries(tree-sitter-python PRIVATE unofficial::tree-sitter::tree-sitter)

add_executable(my_ai 
    src/main.cpp 
    src/parser.cpp
    src/graph.cpp
    src/client.cpp
    src/patcher.cpp
)

# Link libraries
target_link_libraries(my_ai PRIVATE 
    cxxopts::cxxopts
    cpr::cpr
    nlohmann_json::nlohmann_json
    fmt::fmt
    unofficial::tree-sitter::tree-sitter
    tree-sitter-cpp
    tree-sitter-python
)
