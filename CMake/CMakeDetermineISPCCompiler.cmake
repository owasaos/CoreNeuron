# Find the compiler
find_program(
    CMAKE_FOO_COMPILER
        NAMES "ispc"
        HINTS "${CMAKE_SOURCE_DIR}"
        DOC "ISPC compiler"
)
mark_as_advanced(CMAKE_ISPC_COMPILER)

set(CMAKE_ISPC_SOURCE_FILE_EXTENSIONS ispc)
set(CMAKE_ISPC_OUTPUT_EXTENSION .obj)
set(CMAKE_ISPC_COMPILER_ENV_VAR "ISPC")

# Configure variables set in this file for fast reload later on
configure_file(${CMAKE_CURRENT_LIST_DIR}/CMakeISPCCompiler.cmake.in
               ${CMAKE_PLATFORM_INFO_DIR}/CMakeISPCCompiler.cmake)