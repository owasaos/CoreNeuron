# Copyright (c) 2019, Blue Brain Project
# All rights reserved.

# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.

# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
# THE POSSIBILITY OF SUCH DAMAGE.


include_directories(utils/randoms)
include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${MPI_INCLUDE_PATH})

file(GLOB_RECURSE coreneuron_all_headers "*.h*")
file(GLOB_RECURSE coreneuron_all_templates "*.ipp")
file(GLOB_RECURSE coreneuron_cuda_files "*.cu")
file(GLOB_RECURSE coreneuron_all_c_files "nrniv/*.c*" "nrnmpi/*.c*" "nrnoc/*.c*"
                                         "nrnomp/*.c*" "utils/*.c*")

set(DIMPLIC_C "mech/dimplic.cpp")
file(GLOB_RECURSE scopmath_c_files "scopmath_core/*.c*")


# Compile and include MOD files

# set mod2c binary and files from built in
if(NOT EXISTS ${MOD2C})
    if(${ENABLE_ISPC_TARGET})
        message(FATAL_ERROR "ispc targets can only be generated with the external nmodl compiler")
    endif()
    set (MOD2C ${PROJECT_BINARY_DIR}/external/mod2c/bin/mod2c_core)
endif()

set (MOD2C_BINARY ${MOD2C})
get_filename_component(mod2c_install_prefix ${MOD2C} DIRECTORY)
set (MOD2C_UNITS "${PROJECT_BINARY_DIR}/external/mod2c/share/nrnunits.lib")
set(MOD2C env "MODLUNIT=${MOD2C_UNITS}" "PYTHONPATH=$ENV{PYTHONPATH}" ${MOD2C})

message(STATUS "USING MOD2C ${MOD2C}")

# Macro sets up build rule for .c files from .mod files.
# Parameters:
#    KEY name               An arbitrary name to keep track of output .c files
#    MODFILE input          The path to the mod file
#    OUTTYPE <SERIAL|ISPC>  The output type (optional, defaults to serial)
#
# Because mod2c_core wants to write the .c file in the same directory
# as the mod file, we copy the mod file to the binary directory first
#
# The macro appends the names of the output files to MOD2C_${name}_OUTPUTS
# and the names of the mod files (without directories) to MOD2C_${name}_MODS

macro(mod2c_target)
    set(options)
    set(oneValueArgs TARGET MODFILE KEY)
    set(multiValueArgs)
    cmake_parse_arguments(mod2c "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    if("${mod2c_MODFILE}" STREQUAL "")
        message(FATAL_ERROR "mod2c_target requires a MODFILE argument")
    endif()
    if("${mod2c_KEY}" STREQUAL "")
        message(FATAL_ERROR "mod2c_target requires a KEY argument")
    endif()

    get_filename_component(mod2c_source_ ${mod2c_MODFILE} ABSOLUTE)
    get_filename_component(mod2c_modname_ ${mod2c_MODFILE} NAME)

    if("${mod2c_TARGET}" STREQUAL "serial")
        string(REGEX REPLACE "\\.mod$" ".cpp" mod2c_outname_ "${mod2c_modname_}")
        set(nmodl_modearg "--c")
    elseif("${mod2c_TARGET}" STREQUAL "ispc")
        file(STRINGS ${mod2c_MODFILE} mod2c_mod_artcell REGEX "ARTIFICIAL_CELL")
        if(NOT "${mod2c_mod_artcell}" STREQUAL "")
            string(REGEX REPLACE "\\.mod$" ".cpp" mod2c_outname_ "${mod2c_modname_}")
            set(nmodl_modearg "--c")
        else()
            string(REGEX REPLACE "\\.mod$" ".ispc" mod2c_outname_ "${mod2c_modname_}")
            string(REGEX REPLACE "\\.mod$" ".cpp" mod2c_wrapper_outname_ "${mod2c_modname_}")
            set(nmodl_modearg "--ispc")
        endif()
    elseif("${mod2c_TARGET}" STREQUAL "")
        # default case
        string(REGEX REPLACE "\\.mod$" ".cpp" mod2c_outname_ "${mod2c_modname_}")
        set(nmodl_modearg "--c")
    else()
        message(SEND_ERROR "mod2c_target only supports serial and ispc generation for now: \"${mod2c_TARGET}\"")
    endif()


    set(mod2c_output_ "${CMAKE_CURRENT_BINARY_DIR}/${mod2c_outname_}")
    list(APPEND MOD2C_${mod2c_KEY}_OUTPUTS "${mod2c_output_}")
    if(DEFINED mod2c_wrapper_outname_)
        set(mod2c_wrapper_output_ "${CMAKE_CURRENT_BINARY_DIR}/${mod2c_wrapper_outname_}")
        list(APPEND MOD2C_${mod2c_KEY}_OUTPUTS "${mod2c_wrapper_output_}")
        unset(mod2c_wrapper_outname_)
    endif()
    list(APPEND MOD2C_${mod2c_KEY}_MODS "${mod2c_modname_}")
    if(${NMODL_FOUND})
        add_custom_command(OUTPUT "${mod2c_output_}" "${mod2c_wrapper_output_}"
                DEPENDS ${mod2c_MODFILE} ${MOD2C_BINARY}
                COMMAND ${CMAKE_COMMAND} -E copy "${mod2c_source_}" "${CMAKE_CURRENT_BINARY_DIR}"
                COMMAND ${MOD2C} "${mod2c_modname_}" -o "${CMAKE_CURRENT_BINARY_DIR}" host ${nmodl_modearg} ${NMODL_EXTRA_FLAGS}
                WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    else()
        add_custom_command(OUTPUT "${mod2c_output_}"
                DEPENDS ${mod2c_MODFILE} mod2c ${MOD2C_BINARY}
                COMMAND ${CMAKE_COMMAND} -E copy "${mod2c_source_}" "${CMAKE_CURRENT_BINARY_DIR}"
                COMMAND ${MOD2C} "${mod2c_modname_}"
                WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
endmacro()

# Function to check if mod file of specific filename is already added
# to the target. When user specify multiple search directory paths then
# it's important to check if unique filenames exist.
# Parameters:
#   filepath_list   list of absolute filepath of all mod files found
#   filename        (only) filename of mod file

function(file_in_filepath_list filepath_list filename if_exist)
    foreach(filepath ${filepath_list})
        get_filename_component(newfile "${filepath}" NAME)
        if("${newfile}" STREQUAL "${filename}")
            set(${if_exist} TRUE PARENT_SCOPE)
        endif()
    endforeach()
endfunction()

# Macro mod2c_from_file attempts to add modules specified in a file.
# Parameters:
#     name        Key for mod2c_target
#     modlist     Path to the file containing the list of mod files.
#     searchpath  List of directories to search for mod files.
#
# This has to be a macro in order to properly accumulate the MOD2C_xxx_OUTPUTS
# variables. Go CMake! You rock!

macro(mod2c_from_file name modlist searchpath)
    unset(tmp_path_ CACHE)
    if(NOT EXISTS "${modlist}")
        string(REGEX REPLACE ":" ";" spath "${searchpath}")
        foreach(path_ ${spath})
            file(GLOB_RECURSE mod_files "${path_}/*.mod")
            list(APPEND mods_ "${mod_files};")
        endforeach()
    else()
        file(READ "${modlist}" mods_)
    endif()
    string(REGEX REPLACE "[ \t\n]+" ";" mods_ "${mods_}")
    list(REMOVE_ITEM mods_ "")
    list(APPEND added_mod_files "")
    list(APPEND mod_filenames "")

    foreach(mod_ ${mods_})
        unset(modpath_)
        if(NOT IS_ABSOLUTE "${mod_}")
            find_path(tmp_path_ "${mod_}" PATH ${searchpath} NO_DEFAULT_PATH)
            if(tmp_path_)
                set(modpath_ "${tmp_path_}/${mod_}")
            endif()
            unset(tmp_path_ CACHE)
        else()
            set(modpath_ "${mod_}")
        endif()
        if((NOT EXISTS "${modpath_}") OR (IS_DIRECTORY "${modpath_}"))
            unset(modpath_)
        endif()

        if(modpath_)
            get_filename_component(filename "${modpath_}" NAME)
            set(if_exist FALSE)
            file_in_filepath_list("${added_mod_files}" ${filename} if_exist)
            if(${if_exist})
                message(FATAL_ERROR "Multiple ${filename} exist in the search path, list of mod files found : ${modpath_} ${added_mod_files}")
            else()
                list(APPEND added_mod_files "${modpath_}")
                list(APPEND mod_filenames "${filename}")
                mod2c_target(KEY ${name} MODFILE "${modpath_}" TARGET ${M2C_TARGET})
                list(APPEND MOD_PATHS ${modpath_})
            endif()

        else()
            message(WARNING "Unable to locate MOD file: ${mod_}")
        endif()

    endforeach()
    # mod file for gap junction test. this mod file needs to be
    # added only if not found in external directories.
    set(if_exist FALSE)
    file_in_filepath_list("${mods_}" "halfgap.mod" if_exist)

    if(${if_exist})
        message(WARNING "Note: halfgap.mod is found in specified mechanism search directory!")
    else()
        set(GAPMOD "${PROJECT_SOURCE_DIR}/tests/integration/ring_gap/mod/halfgap.mod")
            mod2c_target(KEY ${name} MODFILE "${GAPMOD}" TARGET ${M2C_TARGET})
        list(APPEND MOD_PATHS ${GAPMOD})
        message(STATUS "Adding halfgap.mod mod file for tests!")
    endif()

    message(STATUS "MOD files used : ${mod_filenames}")
endmacro()

# Standard MOD files are always built

set(STDMODLIBPATH "${CMAKE_CURRENT_SOURCE_DIR}/mech/modfile")
set(MECH_STDMODS expsyn.mod exp2syn.mod  hh.mod  netstim.mod  passive.mod  pattern.mod stim.mod)

if (EXPORT_MECHS_FUNCTIONS)
  # Add 6 standard mechanisms to MOD_PATHS
  foreach(MOD ${MECH_STDMODS})
      list(APPEND MOD_PATHS "${STDMODLIBPATH}/${MOD}")
  endforeach()
endif()
if(${ENABLE_ISPC_TARGET})
    set(M2C_TARGET ispc)
else()
    set(M2C_TARGET serial)
endif()

foreach(MOD ${MECH_STDMODS})
    mod2c_target(KEY STDMECH MODFILE "${STDMODLIBPATH}/${MOD}" TARGET ${M2C_TARGET})
endforeach()

set(MECH_FILE_LISTS)

# Additional optional mechanisms specified in ADDITIONAL_MECHS
# and ADDITIONAL_MECHPATH (set as a PATH cache variable, but
# can in fact be a list of paths.)
# If ADDITIONAL_MECHS is not provided, the entire ADDITIONAL_MECHPATH
# will be checked for the mod files.

if(EXISTS ${ADDITIONAL_MECHS})
    if(IS_DIRECTORY "${modlist}")
        message(WARNING "Unable to read MOD list file: ${modlist}")
    else()
        list(APPEND MECH_FILE_LISTS "${ADDITIONAL_MECHS}")
    endif()
endif()

mod2c_from_file(OPTMECH "${ADDITIONAL_MECHS}" "${ADDITIONAL_MECHPATH}")

# For 'non-standard' mod files, need to generate registering function in mod_func.c

set(MOD_FUNC_C    "${CMAKE_CURRENT_BINARY_DIR}/mod_func.cpp")
set(MOD_FUNC_C_PL "${CMAKE_CURRENT_SOURCE_DIR}/mech/mod_func.c.pl")

# ... pass as argument to the perl script the list of mod file names;
add_custom_command(OUTPUT "${MOD_FUNC_C}"
    DEPENDS ${MECH_FILE_LISTS} "${MOD_FUNC_C_PL}"
    COMMAND perl "${MOD_FUNC_C_PL}" ${MOD2C_OPTMECH_MODS} > "${MOD_FUNC_C}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

# ... and add a reconfigure dependency on the files that listed them

foreach(depfile ${MECH_FILE_LISTS})
    configure_file("${depfile}" "${CMAKE_CURRENT_BINARY_DIR}/dummy_reconfigure_dep")
endforeach()

# to work around no acc ability to pass function pointers as arguments,
# some translated c files depend on a _kinderiv.h file that is constructed
# by kinderiv.py

set(KINDERIV_PY "${CMAKE_CURRENT_SOURCE_DIR}/kinderiv.py")
set(KINDERIV_H "${CMAKE_CURRENT_BINARY_DIR}/_kinderiv.h")

add_custom_command(OUTPUT "${KINDERIV_H}"
    DEPENDS ${MOD2C_OPTMECH_OUTPUTS} ${MOD2C_STDMECH_OUTPUTS} "${KINDERIV_PY}"
    COMMAND python "${KINDERIV_PY}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_custom_target(kinderiv DEPENDS "${KINDERIV_H}")

if (EXPORT_MECHS_FUNCTIONS)
  # Create C file with all "get function pointers" methods
  set(MOD_FUNC_PTRS_C    "${CMAKE_CURRENT_BINARY_DIR}/mod_func_ptrs.cpp")
  set(MOD_FUNC_PTRS_C_PL "${CMAKE_CURRENT_SOURCE_DIR}/mech/mod_func_ptrs.c.pl")

  # ... pass as argument to the perl script the list of mods full paths;
  add_custom_command(OUTPUT "${MOD_FUNC_PTRS_C}"
      DEPENDS ${MECH_FILE_LISTS} "${MOD_FUNC_PTRS_C_PL}"
      COMMAND perl "${MOD_FUNC_PTRS_C_PL}" ${MOD_PATHS} > "${MOD_FUNC_PTRS_C}"
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endif()


set_source_files_properties(${MOD2C_STDMECH_OUTPUTS} PROPERTIES GENERATED TRUE)
set_source_files_properties(${MOD2C_OPTMECH_OUTPUTS} PROPERTIES GENERATED TRUE)


if(ENABLE_ISPC_TARGET)
    set(MOD2C_ISPC_STDMECH_OUTPUTS ${MOD2C_STDMECH_OUTPUTS})
    list(FILTER MOD2C_ISPC_STDMECH_OUTPUTS INCLUDE REGEX ".*\.ispc$")
    set(MOD2C_ISPC_OPTMECH_OUTPUTS ${MOD2C_OPTMECH_OUTPUTS})
    list(FILTER MOD2C_ISPC_OPTMECH_OUTPUTS INCLUDE REGEX ".*\.ispc$")
    set_source_files_properties(${MOD2C_ISPC_STDMECH_OUTPUTS} PROPERTIES LANGUAGE ISPC)
    set_source_files_properties(${MOD2C_ISPC_STDMECH_OUTPUTS} PROPERTIES COMPILE_FLAGS "-I${nmodl_INCLUDE}")
    set_source_files_properties(${MOD2C_ISPC_OPTMECH_OUTPUTS} PROPERTIES LANGUAGE ISPC)
    set_source_files_properties(${MOD2C_ISPC_OPTMECH_OUTPUTS} PROPERTIES COMPILE_FLAGS "-I${nmodl_INCLUDE}")
endif()


# Add generated sources from MOD files
set(ADDITIONAL_MECH_C_FILES ${MOD_FUNC_C} ${MOD_FUNC_PTRS_C} ${DIMPLIC_C} ${MOD2C_OPTMECH_OUTPUTS})
set(GENERATED_MECH_C_FILES ${ADDITIONAL_MECH_C_FILES}  ${MOD2C_STDMECH_OUTPUTS})

# artificial cells must be on cpu, defaul nrnran123.c is for cpu, nrn_setup.cpp uses nrnran123 for only memory calculation purpose which should use cpu version of nrnran123
set(NOACC_MECH_C_FILES
    ${CMAKE_CURRENT_BINARY_DIR}/netstim.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/netstim_inhpoisson.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/pattern.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/randoms/nrnran123.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/nrniv/nrn_setup.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/nrniv/global_vars.cpp)

if(ENABLE_OPENACC)
    set_source_files_properties(${GENERATED_MECH_C_FILES} PROPERTIES COMPILE_FLAGS "")
    set_source_files_properties(${NOACC_MECH_C_FILES} PROPERTIES COMPILE_FLAGS "-DDISABLE_OPENACC")
    if(${CMAKE_C_COMPILER_ID} STREQUAL "PGI")
        set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/scopmath_core/sparse_thread.c PROPERTIES COMPILE_FLAGS "-ta=tesla:nollvm")
    endif()
endif()

#to indent generated c/cpp files
if(CLANG_FORMAT_FOUND)
    add_custom_target(formatbuild
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_FILES:STRING="${GENERATED_MECH_C_FILES}"
            -DCLANG_FORMAT_EXECUTABLE=${CLANG_FORMAT_EXECUTABLE}
            -P "${PROJECT_SOURCE_DIR}/CMake/ClangFormatUtils.cmake"
        DEPENDS ${GENERATED_MECH_C_FILES}
        )
endif()


set(link_cudacoreneuron)
set(link_reportinglib)

#@TODO: CMake should have option for arch
if(ENABLE_OPENACC AND ENABLE_CUDA_MODULES)
    cuda_add_library("cudacoreneuron" ${coreneuron_cuda_files} OPTIONS
        -arch=sm_30
        -gencode=arch=compute_30,code=sm_30
        -gencode=arch=compute_50,code=sm_50
        -gencode=arch=compute_52,code=sm_52
        -gencode=arch=compute_52,code=compute_52
        -Xcompiler -fPIC
    )
    set(link_cudacoreneuron cudacoreneuron)
endif()

if(ENABLE_REPORTINGLIB)
    set(link_reportinglib ${reportinglib_LIBRARY})
endif()



############################################################
# Target libs
############################################################

#
# The coreneuron lib (only internal mechanisms)
#
add_library(coreneuron ${COMPILE_LIBRARY_TYPE} ${coreneuron_all_headers} ${coreneuron_all_templates}
                       ${coreneuron_all_c_files} ${cudacorenrn_objs} ${MOD2C_STDMECH_OUTPUTS})
add_dependencies(coreneuron kinderiv)

target_link_libraries(coreneuron ${MPI_CXX_LIBRARIES}
    ${link_reportinglib} ${link_cudacoreneuron} ${CUDA_LIBRARIES} ${CALIPER_LIB} ${CALIPER_MPI_LIB})

set_target_properties(coreneuron PROPERTIES
                      VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}
                      SOVERSION ${VERSION_MAJOR}
                      CLEAN_DIRECT_OUTPUT 1)

set_property(TARGET coreneuron PROPERTY POSITION_INDEPENDENT_CODE ON)

# Extract link defs to the whole project
get_target_property(CORENRN_LINK_LIBS coreneuron LINK_LIBRARIES)
if(NOT CORENRN_LINK_LIBS)
    set(CORENRN_LINK_LIBS "")
endif()
set(CORENRN_LINK_LIBS "${CORENRN_LINK_LIBS}" PARENT_SCOPE)

# Share built-in OPTIONAL MODS also with rest of project
set(CORENRN_OPTMECH_MODS "${MOD2C_OPTMECH_MODS}" PARENT_SCOPE)


# The scopmath lib - should not contain dimplic
add_library(scopmath STATIC ${coreneuron_all_headers} ${scopmath_c_files})
set_target_properties(scopmath PROPERTIES POSITION_INDEPENDENT_CODE ON)

#
# The "complete" lib (coreneuron + opt mechs)
#
set(ENGINEMECH_C "mech/enginemech.cpp")
add_library(corenrnmech ${COMPILE_LIBRARY_TYPE} ${coreneuron_all_headers} ${ENGINEMECH_C} ${ADDITIONAL_MECH_C_FILES})
add_dependencies(corenrnmech kinderiv)

# The mechanism registration function will be always defined (at least for halfgap)
target_compile_definitions(corenrnmech PUBLIC -DADDITIONAL_MECHS)

# Public link interface (default), so that end binary links with all, even if they'r static
target_link_libraries(corenrnmech scopmath coreneuron)


#########################################################
# INSTALL
#########################################################

install(TARGETS coreneuron corenrnmech ${link_cudacoreneuron}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(FILES "engine.h" "enginemech.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/coreneuron)

install(PROGRAMS ${MOD2C_BINARY}
        DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES ${MOD2C_UNITS}
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mod2c)


if(ENABLE_HEADER_INSTALL)
    install(DIRECTORY .
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/coreneuron
            FILES_MATCHING PATTERN "*.h*" PATTERN "*.ipp")

    install(FILES ${KINDERIV_PY} ${MOD_FUNC_C_PL} ${DIMPLIC_C} ${ENGINEMECH_C}
            DESTINATION ${CMAKE_INSTALL_DATADIR}/coreneuron)

    install(TARGETS scopmath
            DESTINATION ${CMAKE_INSTALL_DATADIR}/coreneuron)

    install(FILES ${MOD2C_OPTMECH_OUTPUTS}
            DESTINATION ${CMAKE_INSTALL_DATADIR}/mod2c)
endif()
