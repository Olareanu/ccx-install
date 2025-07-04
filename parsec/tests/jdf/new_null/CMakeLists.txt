include(ParsecCompilePTG)

#
# Test that NULL as output returns an error
#
add_test(NAME unit_jdf_output_NULL_ptgpp
  COMMAND ${parsec_ptgpp_EXE} ${PARSEC_PTGPP_CFLAGS} ${ADDITIONAL_PTGPP_CFLAGS} -E -i ${CMAKE_CURRENT_SOURCE_DIR}/output_NULL.jdf -o output_NULL -f output_NULL)
set_tests_properties(unit_jdf_output_NULL_ptgpp
  PROPERTIES
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/output_NULL.jdf)

add_test(NAME unit_jdf_output_NULL_true_ptgpp
  COMMAND ${parsec_ptgpp_EXE} ${PARSEC_PTGPP_CFLAGS} ${ADDITIONAL_PTGPP_CFLAGS} -E -i ${CMAKE_CURRENT_SOURCE_DIR}/output_NULL_true.jdf -o output_NULL_true -f output_NULL_true)
set_tests_properties(unit_jdf_output_NULL_true_ptgpp
  PROPERTIES
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/output_NULL_true.jdf)

add_test(NAME unit_jdf_output_NULL_false_ptgpp
  COMMAND ${parsec_ptgpp_EXE} ${PARSEC_PTGPP_CFLAGS} ${ADDITIONAL_PTGPP_CFLAGS} -E -i ${CMAKE_CURRENT_SOURCE_DIR}/output_NULL_false.jdf -o output_NULL_false -f output_NULL_false)
set_tests_properties(unit_jdf_output_NULL_false_ptgpp
  PROPERTIES
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/output_NULL_false.jdf)

set_tests_properties(unit_jdf_output_NULL_ptgpp unit_jdf_output_NULL_true_ptgpp unit_jdf_output_NULL_false_ptgpp
  PROPERTIES
  DEPENDS "${parsec_ptgpp_EXE}"
  PASS_REGULAR_EXPRESSION "NULL data only supported in IN dependencies.")

#
# Test that NEW as output returns an error
#
add_test(NAME unit_jdf_output_NEW_ptgpp
  COMMAND ${parsec_ptgpp_EXE} ${PARSEC_PTGPP_CFLAGS} ${ADDITIONAL_PTGPP_CFLAGS} -E -i ${CMAKE_CURRENT_SOURCE_DIR}/output_NEW.jdf -o output_NEW -f output_NEW)
set_tests_properties(unit_jdf_output_NEW_ptgpp
  PROPERTIES
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/output_NEW.jdf)

add_test(NAME unit_jdf_output_NEW_true_ptgpp
  COMMAND ${parsec_ptgpp_EXE} ${PARSEC_PTGPP_CFLAGS} ${ADDITIONAL_PTGPP_CFLAGS} -E -i ${CMAKE_CURRENT_SOURCE_DIR}/output_NEW_true.jdf -o output_NEW_true -f output_NEW_true)
set_tests_properties(unit_jdf_output_NEW_true_ptgpp
  PROPERTIES
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/output_NEW_true.jdf)

add_test(NAME unit_jdf_output_NEW_false_ptgpp
  COMMAND ${parsec_ptgpp_EXE} ${PARSEC_PTGPP_CFLAGS} ${ADDITIONAL_PTGPP_CFLAGS} -E -i ${CMAKE_CURRENT_SOURCE_DIR}/output_NEW_false.jdf -o output_NEW_false -f output_NEW_false)
set_tests_properties(unit_jdf_output_NEW_false_ptgpp
  PROPERTIES
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/output_NEW_false.jdf)

set_tests_properties(unit_jdf_output_NEW_ptgpp unit_jdf_output_NEW_true_ptgpp unit_jdf_output_NEW_false_ptgpp
  PROPERTIES
  DEPENDS "${parsec_ptgpp_EXE}"
  PASS_REGULAR_EXPRESSION "Automatic data allocation with NEW only supported in IN dependencies."
  )

#
# Test that a NULL cannot be forwarded
#
parsec_compile_ptg(forward_RW_NULL "${CMAKE_CURRENT_SOURCE_DIR}/forward_RW_NULL.jdf")
parsec_addtest(C jdf_forward_RW_NULL "${forward_RW_NULL}")

parsec_compile_ptg(forward_READ_NULL "${CMAKE_CURRENT_SOURCE_DIR}/forward_READ_NULL.jdf")
parsec_addtest(C jdf_forward_READ_NULL "${forward_READ_NULL}")

add_test(unit_jdf_forward_RW_NULL_shm   ${SHM_TEST_CMD_LIST}       ./jdf_forward_RW_NULL)
add_test(unit_jdf_forward_READ_NULL_shm ${SHM_TEST_CMD_LIST}       ./jdf_forward_READ_NULL)
set_tests_properties(
  unit_jdf_forward_RW_NULL_shm
  unit_jdf_forward_READ_NULL_shm
  PROPERTIES
  PASS_REGULAR_EXPRESSION "A NULL is forwarded"
)

if( MPI_C_FOUND )
  add_test(unit_jdf_forward_RW_NULL_mpi   ${MPI_TEST_CMD_LIST} 2 ./jdf_forward_RW_NULL)
  add_test(unit_jdf_forward_READ_NULL_mpi ${MPI_TEST_CMD_LIST} 2 ./jdf_forward_READ_NULL)
  set_tests_properties(
    unit_jdf_forward_RW_NULL_mpi
    unit_jdf_forward_READ_NULL_mpi
    PROPERTIES
    PASS_REGULAR_EXPRESSION "A NULL is forwarded"
  )
endif( MPI_C_FOUND )

