#
#  Settings for targets
#
set(BASE_SOURCES
  class/parsec_dequeue.c
  class/parsec_fifo.c
  class/parsec_lifo.c
  class/parsec_list.c
  class/parsec_object.c
  class/parsec_value_array.c
  class/parsec_hash_table.c
  class/parsec_rwlock.c
  utils/argv.c
  utils/cmd_line.c
  utils/colors.c
  utils/parsec_environ.c
  utils/installdirs.c
  utils/keyval_parse.c
  utils/mca_param.c
  utils/mca_param_cmd_line.c
  utils/mca_parse_paramfile.c
  utils/os_path.c
  utils/output.c
  utils/show_help.c
  utils/zone_malloc.c
  utils/atomic_external.c
  utils/debug.c
)

FLEX_TARGET(show_help_flex utils/show_help_lex.l ${CMAKE_CURRENT_BINARY_DIR}/show_help_lex.l.c)
SET_SOURCE_FILES_PROPERTIES(${FLEX_show_help_flex_OUTPUTS} PROPERTIES COMPILE_FLAGS "${CMAKE_C_FLAGS}")
list(APPEND BASE_SOURCES ${FLEX_show_help_flex_OUTPUTS})

FLEX_TARGET(keyval_flex utils/keyval_lex.l ${CMAKE_CURRENT_BINARY_DIR}/keyval_lex.l.c)
SET_SOURCE_FILES_PROPERTIES(${FLEX_keyval_flex_OUTPUTS} PROPERTIES COMPILE_FLAGS "${CMAKE_C_FLAGS}")
list(APPEND BASE_SOURCES ${FLEX_keyval_flex_OUTPUTS})

# Read the Modular Components
#   This must be read using include and not add_subdirectory because
#   we want the mca/CMakeLists.txt to export the MCA_EXTRA_SOURCES it builds.
include(mca/CMakeLists.txt)

# Import all the available interfaces
include(interfaces/CMakeLists.txt)

if( PARSEC_PAPI_SDE )
  list(APPEND EXTRA_SOURCES "papi_sde.c")
endif( PARSEC_PAPI_SDE )

# find_package(TAU REQUIRED)
# include_directories(${TAU_INCLUDE_DIRS})
# message(WARNING ${TAU_INCLUDE_DIRS})
# list(APPEND EXTRA_LIBS ${TAU_LIBRARIES})
# set (CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib" ${TAU_LIBRARY_DIR})

if(PARSEC_PROF_TRACE)
  if(PARSEC_PROF_TRACE_SYSTEM STREQUAL "OTF2" OR PARSEC_PROF_TRACE_SYSTEM STREQUAL "Auto")
    find_package(OTF2 2.1.1)
    if(OTF2_FOUND)
      set(PROFILING_SOURCE "profiling_otf2.c")
      list(APPEND EXTRA_LIBS "otf2")
      MESSAGE(STATUS "Profiling uses OTF2")
    else(OTF2_FOUND)
      if(PARSEC_PROF_TRACE_SYSTEM STREQUAL "OTF2")
	MESSAGE(FATAL_ERROR "Requested OTF2 tracing system is not available because OTF2 was not found")
      else(PARSEC_PROF_TRACE_SYSTEM STREQUAL "OTF2")
	set(PROFILING_SOURCE "profiling.c")
	MESSAGE(STATUS "Profiling uses PaRSEC Binary Tracing Format")
      endif(PARSEC_PROF_TRACE_SYSTEM STREQUAL "OTF2")
    endif(OTF2_FOUND)
  else(PARSEC_PROF_TRACE_SYSTEM STREQUAL "OTF2" OR PARSEC_PROF_TRACE_SYSTEM STREQUAL "Auto")
    set(PROFILING_SOURCE "profiling.c")
    MESSAGE(STATUS "Profiling uses PaRSEC Binary Tracing Format")
  endif(PARSEC_PROF_TRACE_SYSTEM STREQUAL "OTF2" OR PARSEC_PROF_TRACE_SYSTEM STREQUAL "Auto")
endif(PARSEC_PROF_TRACE)

set(SOURCES
  arena.c
  barrier.c
  bindthread.c
  parsec.c
  data.c
  data_distribution.c
  debug_marks.c
  devices/device.c
  mca/mca_repository.c
  mempool.c
  private_mempool.c
  ${PROFILING_SOURCE}
  remote_dep.c
  scheduling.c
  vpmap.c
  maxheap.c
  hbbuffer.c
  datarepo.c
  ${EXTRA_SOURCES}
  ${MCA_EXTRA_SOURCES}
)

if( NOT MPI_C_FOUND )
  list(APPEND SOURCES datatype/datatype.c)
else( NOT MPI_C_FOUND )
  list(APPEND SOURCES datatype/datatype_mpi.c)
endif( NOT MPI_C_FOUND )
list(APPEND SOURCES parsec_hwloc.c)

if (CUDA_FOUND)
  list(APPEND SOURCES devices/cuda/dev_cuda.c devices/cuda/transfer.c)
  install(FILES
          ${CMAKE_CURRENT_SOURCE_DIR}/devices/cuda/dev_cuda.h
          DESTINATION include/parsec/devices/cuda )
endif (CUDA_FOUND)

if( PARSEC_PROF_GRAPHER )
  list(APPEND SOURCES parsec_prof_grapher.c)
endif( PARSEC_PROF_GRAPHER )

add_subdirectory(data_dist)
#
# Setup targets
#
if( BUILD_PARSEC )
  # Build this as a collection of objects that can be easily either used to
  # create an interface library or imported into the main library.
  add_library(parsec-base-obj OBJECT ${BASE_SOURCES})
  set_target_properties(parsec-base-obj PROPERTIES COMPILE_FLAGS "-DYYERROR_VERBOSE")
if(BUILD_SHARED_LIBS)
  set_target_properties(parsec-base-obj PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif(BUILD_SHARED_LIBS)

  add_library(parsec-base STATIC $<TARGET_OBJECTS:parsec-base-obj>)

  # Create an interface library to allow the absorbtion of the object files into
  # the PaRSEC library.
  add_library(parsec-base-iface INTERFACE)
  target_sources(parsec-base-iface INTERFACE $<TARGET_OBJECTS:parsec-base-obj>)

  add_library(parsec ${SOURCES})
  set_target_properties(parsec PROPERTIES COMPILE_FLAGS "-DYYERROR_VERBOSE")
if(BUILD_SHARED_LIBS)
  set_target_properties(parsec PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif(BUILD_SHARED_LIBS)
# For now make sure that the installed shared libraries have the right
# version (same version as the major of the build version). Thus, every
# change in the high-level API (and ABI) should be reflected on the
# major version of PaRSEC.
  set_target_properties(parsec PROPERTIES VERSION ${PARSEC_VERSION_MAJOR}.${PARSEC_VERSION_MINOR}.${PARSEC_VERSION_PATCH}
                                        SOVERSION ${PARSEC_VERSION_MAJOR})
  if (MPI_C_FOUND)
    set_target_properties(parsec PROPERTIES COMPILE_FLAGS "${MPI_C_COMPILE_FLAGS}")
  endif (MPI_C_FOUND)

  if( PARSEC_PAPI_SDE )
    # This file is in fact provided by PAPI SDE, to serve as a fallback in case PAPI
    # is not selected / loaded. It should be dynamic library that comes after PAPI
    add_library(parsec-papi-sde papi_sde_interface.c)
    # parsec-papi-sda must come last in EXTRA_LIBS: it is to be preceded by papi if
    # PAPI is loaded so that the weak symbols in it are overcome by PAPI
    list(APPEND EXTRA_LIBS parsec-papi-sde)
    # Propagate the updates to the parent
    set(EXTRA_LIBS ${EXTRA_LIBS} PARENT_SCOPE)
    # parsec-papi-sde has to be installed, to provide the default empty functions
    install(TARGETS parsec-papi-sde DESTINATION lib)
  endif( PARSEC_PAPI_SDE )

  target_link_libraries(parsec
                        PRIVATE parsec_data parsec-base-iface ${EXTRA_LIBS})

#
# If we have support for F90 build the PaRSEC module
#
  if(CMAKE_Fortran_COMPILER_SUPPORTS_F90)
    include(FortranCInterface)
    FortranCInterface_HEADER(fortran/f2c_mangle.h
                             MACRO_NAMESPACE "PARSEC_F2C_"
                             SYMBOL_NAMESPACE "parsec_f2c_"
                             SYMBOLS my_sub parsecf:my_sub)
    add_subdirectory(fortran)
    target_link_libraries(parsec
                          PRIVATE parsec_fortran)
  endif(CMAKE_Fortran_COMPILER_SUPPORTS_F90)

  install(TARGETS parsec
          DESTINATION lib)

  install(FILES utils/help-mca-param.txt DESTINATION share/parsec)
ENDIF( BUILD_PARSEC )

