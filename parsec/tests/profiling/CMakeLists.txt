include(ParsecCompilePTG)

#parsec_compile_ptg(ep_generated "${CMAKE_CURRENT_SOURCE_DIR}/ep.jdf")
#string( REGEX REPLACE "ep.[hc]" "" include_dir "${ep_generated}" )
include_directories(${include_dir})

if(MPI_Fortran_FOUND AND CMAKE_Fortran_COMPILER_WORKS)
  if(CMAKE_Fortran_COMPILER_SUPPORTS_F90)
    parsec_addtest(Fortran generate_f "generate_f.F90")
    target_link_libraries(generate_f parsec-base)
    SET_TARGET_PROPERTIES(generate_f PROPERTIES LINKER_LANGUAGE Fortran)
    SET_TARGET_PROPERTIES(generate_f PROPERTIES COMPILE_FLAGS "-I${PROJECT_BINARY_DIR}/parsec/fortran")
  endif(CMAKE_Fortran_COMPILER_SUPPORTS_F90)
endif(MPI_Fortran_FOUND AND CMAKE_Fortran_COMPILER_WORKS)

