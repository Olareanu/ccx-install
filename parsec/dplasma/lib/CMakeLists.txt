# The current version number
set (DPLASMA_VERSION_MAJOR 2)
set (DPLASMA_VERSION_MINOR 0)
set (DPLASMA_VERSION_PATCH 0)

include(PrecisionGenerator)
include(ParsecCompilePTG)

# reset variables
set(generated_files "")
set(generated_jdf "")

set(EXTRA_SOURCES
  dplasmaaux.c
  dplasma_hqr.c
  dplasma_hqr_dbg.c
  dplasma_systolic_qr.c
  map2_wrapper.c
  butterfly_map.c
)
if( NOT PARSEC_HAVE_COMPLEX_H )
  list(APPEND EXTRA_SOURCES complex.c)
endif()



### Generate .c files from .jdf for all required precisions
set(JDF
  # Auxiliary
  zlange_frb_cyclic.jdf zlange_one_cyclic.jdf
  zlansy.jdf zlanm2.jdf
  zlaswp.jdf
  zpltmg_chebvand.jdf zpltmg_fiedler.jdf zpltmg_hankel.jdf zpltmg_toeppd.jdf
  zprint.jdf
  #
  # BLAS Level 3
  #
  zgemm_NN.jdf zgemm_TN.jdf zgemm_NT.jdf zgemm_TT.jdf
  zgemm_NN_summa.jdf zgemm_TN_summa.jdf zgemm_NT_summa.jdf zgemm_TT_summa.jdf
  zhemm.jdf
  zher2k_LN.jdf zher2k_LC.jdf zher2k_UN.jdf zher2k_UC.jdf
  zherk_LN.jdf zherk_LC.jdf zherk_UN.jdf zherk_UC.jdf
  zsymm.jdf
  zsyr2k_LN.jdf zsyr2k_LT.jdf zsyr2k_UN.jdf zsyr2k_UT.jdf
  zsyrk_LN.jdf zsyrk_LT.jdf zsyrk_UN.jdf zsyrk_UT.jdf
  ztrmm_LLN.jdf ztrmm_LLT.jdf ztrmm_LUN.jdf ztrmm_LUT.jdf ztrmm_RLN.jdf ztrmm_RLT.jdf ztrmm_RUN.jdf ztrmm_RUT.jdf
  ztrsm_LLN.jdf ztrsm_LLT.jdf ztrsm_LUN.jdf ztrsm_LUT.jdf ztrsm_RLN.jdf ztrsm_RLT.jdf ztrsm_RUN.jdf ztrsm_RUT.jdf
  # BLAS 2
  zger.jdf
  #
  # Lapack
  #
  # Cholesky
  zpotrf_U.jdf zpotrf_L.jdf
  ztrtri_L.jdf ztrtri_U.jdf
  zlauum_L.jdf zlauum_U.jdf
  zpoinv_L.jdf zpoinv_U.jdf
  # LQ/QR
  zgelqf.jdf
  zgeqrf.jdf
  zungqr.jdf
  zunglq.jdf
  zunmqr_LC.jdf zunmqr_LN.jdf zunmqr_RN.jdf zunmqr_RC.jdf
  zunmlq_LC.jdf zunmlq_LN.jdf zunmlq_RN.jdf zunmlq_RC.jdf
  zgeqrf_param.jdf
  zgelqf_param.jdf
  zungqr_param.jdf
  zunglq_param.jdf
  zunmqr_param_LC.jdf zunmqr_param_LN.jdf zunmqr_param_RN.jdf zunmqr_param_RC.jdf
  zunmlq_param_LC.jdf zunmlq_param_LN.jdf zunmlq_param_RN.jdf zunmlq_param_RC.jdf
  zgeqrfr_geqrt.jdf
  zgeqrfr_unmqr.jdf
  zgeqrfr_tsqrt.jdf
  zgeqrfr_tsmqr.jdf
  # LU
  zgetrf.jdf
  zgetrf_incpiv.jdf
  zgetrf_incpiv_sd.jdf
  zgetrf_nopiv.jdf
  zgetrf_qrf.jdf
  ztrsmpl.jdf
  ztrsmpl_sd.jdf
  ztrsmpl_qrf.jdf
  # Other
  zgebrd_ge2gb.jdf
  zgebmm.jdf
  zgebut.jdf
  zhetrf.jdf
  zhbrdt.jdf
  zhebut.jdf
  zherbt_L.jdf
  zhetrd_h2b_L.jdf
  zhetrd_b2s.jdf
  ztrdsm.jdf
  ztrmdm.jdf
  zgetrf_ptgpanel.jdf
  ztrsmpl_ptgpanel.jdf
)
precisions_rules_py(generated_jdf
                    "${JDF}"
                    PRECISIONS "${DPLASMA_PRECISIONS}")

include_directories(BEFORE "${CMAKE_CURRENT_SOURCE_DIR}")
if( NOT ${CMAKE_CURRENT_BINARY_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR} )
  include_directories(BEFORE "${CMAKE_CURRENT_BINARY_DIR}")
#  foreach(src_file ${EXTRA_SOURCES})
#    set_source_files_properties(${src_file} PROPERTIES COMPILE_FLAGS "-I.")
#  endforeach()
endif( NOT ${CMAKE_CURRENT_BINARY_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR} )

list(APPEND generated_jdf
  ${CMAKE_CURRENT_SOURCE_DIR}/map2.jdf
)

parsec_compile_ptg(generated_files "${generated_jdf}")

### Generate the dplasma wrappers for all required precisions
set(SOURCES
  # Lapack Auxiliary
  zgeadd_wrapper.c
  zlacpy_wrapper.c
  zlange_wrapper.c
  zlanhe_wrapper.c
  zlansy_wrapper.c
  zlantr_wrapper.c
  zlanm2_wrapper.c
  zlascal_wrapper.c
  zlaset_wrapper.c
  zlaswp_wrapper.c
  zplghe_wrapper.c
  zplgsy_wrapper.c
  zplrnt_wrapper.c
  zpltmg_wrapper.c
  zlatms_wrapper.c
  zprint_wrapper.c
  # Level 3 Blas
  zgemm_wrapper.c
  zhemm_wrapper.c
  zher2k_wrapper.c
  zherk_wrapper.c
  zsymm_wrapper.c
  zsyr2k_wrapper.c
  zsyrk_wrapper.c
  ztrmm_wrapper.c
  ztrsm_wrapper.c
  # Level 2 Blas
  zger_wrapper.c
  #
  # Lapack
  #
  # Cholesky
  zposv_wrapper.c
  zpotrf_wrapper.c
  zpotrs_wrapper.c
  ztrtri_wrapper.c
  zlauum_wrapper.c
  zpotri_wrapper.c
  zpoinv_wrapper.c
  #
  zgelqf_wrapper.c
  zgelqs_wrapper.c
  zgels_wrapper.c
  zgeqrf_wrapper.c
  zgeqrs_wrapper.c
  zunglq_wrapper.c
  zungqr_wrapper.c
  zunmlq_wrapper.c
  zunmqr_wrapper.c
  zgeqrf_param_wrapper.c
  zgeqrs_param_wrapper.c
  zungqr_param_wrapper.c
  zunmqr_param_wrapper.c
  zgelqf_param_wrapper.c
  zgelqs_param_wrapper.c
  zunglq_param_wrapper.c
  zunmlq_param_wrapper.c
  #
  zgesv_incpiv_wrapper.c
  zgesv_wrapper.c
  zgetrf_incpiv_wrapper.c
  zgetrf_nopiv_wrapper.c
  zgetrf_qrf_wrapper.c
  zgetrf_wrapper.c
  zgetrs_wrapper.c
  zgetrs_incpiv_wrapper.c
  ztrsmpl_wrapper.c
  ztrsmpl_qrf_wrapper.c
  #
  dplasma_zcheck.c
  #
  zgebrd_ge2gb_wrapper.c
  zhbrdt_wrapper.c
  zherbt_wrapper.c
  zhetrd_wrapper.c
  zhebut_wrapper.c
  zhetrf_wrapper.c
  zhetrs_wrapper.c
  ztrdsm_wrapper.c
  zheev_wrapper.c
  #
  zgetrf_ptgpanel_wrapper.c
  ztrsmpl_ptgpanel_wrapper.c
)
precisions_rules_py(generated_wrappers
                    "${SOURCES}"
                    PRECISIONS "${DPLASMA_PRECISIONS}")

### Generate the lib
link_directories(${COREBLAS_LIBRARY_DIRS})
add_library(dplasma
            ${generated_files}
            ${generated_wrappers}
            ${EXTRA_SOURCES})
add_dependencies(dplasma
         dplasma_includes
         dplasma_cores_includes )
if(BUILD_SHARED_LIBS)
  set_target_properties(dplasma PROPERTIES POSITION_INDEPENDENT_CODE True)
endif(BUILD_SHARED_LIBS)
if (MPI_C_FOUND)
  set_target_properties(dplasma PROPERTIES COMPILE_FLAGS "${MPI_C_COMPILE_FLAGS}")
endif (MPI_C_FOUND)
target_link_libraries(dplasma
                      PRIVATE dplasma_cores
                      PUBLIC ${COREBLAS_LIBRARIES} parsec ${EXTRA_LIBS}
                      $<$<BOOL:${CUDA_FOUND}>:${CUDA_cublas_LIBRARY}>)

set_target_properties(dplasma PROPERTIES LINK_FLAGS "${COREBLAS_LDFLAGS}")
set_target_properties(dplasma PROPERTIES VERSION ${DPLASMA_VERSION_MAJOR}.${DPLASMA_VERSION_MINOR}
                                         SOVERSION ${PARSEC_VERSION_MAJOR})

install(TARGETS dplasma
        ARCHIVE DESTINATION ${DPLASMA_INSTALL_PREFIX}/lib
        LIBRARY DESTINATION ${DPLASMA_INSTALL_PREFIX}/lib)

