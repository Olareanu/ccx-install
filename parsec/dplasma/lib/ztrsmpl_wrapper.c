/*
 * Copyright (c) 2010-2019 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 *
 * @precisions normal z -> s d c
 *
 */


#include <core_blas.h>
#include "dplasma.h"
#include "dplasma/lib/dplasmaaux.h"
#include "dplasma/types.h"

#include "ztrsmpl.h"
#include "ztrsmpl_sd.h"

/**
 *******************************************************************************
 *
 * @ingroup dplasma_complex64
 *
 * dplasma_ztrsmpl_New - Generates the taskpool that solves U*x = b, when U has
 * been generated through LU factorization with incremental pivoting strategy
 * See dplasma_zgetrf_incpiv_New().
 *
 * WARNING: The computations are not done by this call.
 *
 *******************************************************************************
 *
 * @param[in] A
 *          Descriptor of the distributed matrix A to be factorized.
 *          On entry, The factorized matrix through dplasma_zgetrf_incpiv_New()
 *          routine.  Elements on and above the diagonal are the elements of
 *          U. Elements below the diagonal are NOT the classic L, but the L
 *          factors obtaines by succesive pivoting.
 *
 * @param[in] L
 *          Descriptor of the matrix L distributed exactly as the A matrix.
 *           - If IPIV != NULL, L.mb defines the IB parameter of the tile LU
 *          algorithm. This matrix must be of size A.mt * L.mb - by - A.nt *
 *          L.nb, with L.nb == A.nb.
 *          On entry, contains auxiliary information required to solve the
 *          system and generated by dplasma_zgetrf_inciv_New().
 *           - If IPIV == NULL, pivoting information are stored within
 *          L. (L.mb-1) defines the IB parameter of the tile LU algorithm. This
 *          matrix must be of size A.mt * L.mb - by - A.nt * L.nb, with L.nb =
 *          A.nb, and L.mb = ib+1.
 *          The first A.mb elements contains the IPIV information, the leftover
 *          contains auxiliary information required to solve the system.
 *
 * @param[in] IPIV
 *          Descriptor of the IPIV matrix. Should be distributed exactly as the
 *          A matrix. This matrix must be of size A.m - by - A.nt with IPIV.mb =
 *          A.mb and IPIV.nb = 1.
 *          On entry, contains the pivot indices of the successive row
 *          interchanged performed during the factorization.
 *          If IPIV == NULL, rows interchange information is stored within L.
 *
 * @param[in,out] B
 *          On entry, the N-by-NRHS right hand side matrix B.
 *          On exit, if return value = 0, B is overwritten by the solution matrix X.
 *
 *******************************************************************************
 *
 * @return
 *          \retval NULL if incorrect parameters are given.
 *          \retval The parsec taskpool describing the operation that can be
 *          enqueued in the runtime with parsec_context_add_taskpool(). It, then, needs to be
 *          destroy with dplasma_ztrsmpl_Destruct();
 *
 *******************************************************************************
 *
 * @sa dplasma_ztrsmpl
 * @sa dplasma_ztrsmpl_Destruct
 * @sa dplasma_ctrsmpl_New
 * @sa dplasma_dtrsmpl_New
 * @sa dplasma_strsmpl_New
 *
 ******************************************************************************/
parsec_taskpool_t *
dplasma_ztrsmpl_New(const parsec_tiled_matrix_dc_t *A,
                    const parsec_tiled_matrix_dc_t *L,
                    const parsec_tiled_matrix_dc_t *IPIV,
                    parsec_tiled_matrix_dc_t *B)
{
    parsec_ztrsmpl_taskpool_t *parsec_trsmpl = NULL; 

    if ( (A->mt != L->mt) || (A->nt != L->nt) ) {
        dplasma_error("dplasma_ztrsmpl_New", "L doesn't have the same number of tiles as A");
        return NULL;
    }
    if ( (IPIV != NULL) && ((A->mt != IPIV->mt) || (A->nt != IPIV->nt)) ) {
        dplasma_error("dplasma_ztrsmpl_New", "IPIV doesn't have the same number of tiles as A");
        return NULL;
    }

    if ( IPIV != NULL ) {
        parsec_trsmpl = parsec_ztrsmpl_new(A,
                                         L,
                                         IPIV,
                                         B );
    }
    else {
        parsec_trsmpl = (parsec_ztrsmpl_taskpool_t*)
            parsec_ztrsmpl_sd_new( A,
                                  L,
                                  NULL,
                                  B );
    }

    /* A */
    dplasma_add2arena_tile( parsec_trsmpl->arenas[PARSEC_ztrsmpl_DEFAULT_ARENA],
                            A->mb*A->nb*sizeof(parsec_complex64_t),
                            PARSEC_ARENA_ALIGNMENT_SSE,
                            parsec_datatype_double_complex_t, A->mb );

    /* IPIV */
    dplasma_add2arena_rectangle( parsec_trsmpl->arenas[PARSEC_ztrsmpl_PIVOT_ARENA],
                                 A->mb*sizeof(int),
                                 PARSEC_ARENA_ALIGNMENT_SSE,
                                 parsec_datatype_int_t, A->mb, 1, -1 );

    /* L */
    dplasma_add2arena_rectangle( parsec_trsmpl->arenas[PARSEC_ztrsmpl_SMALL_L_ARENA],
                                 L->mb*L->nb*sizeof(parsec_complex64_t),
                                 PARSEC_ARENA_ALIGNMENT_SSE,
                                 parsec_datatype_double_complex_t, L->mb, L->nb, -1);

    return (parsec_taskpool_t*)parsec_trsmpl;
}

/**
 *******************************************************************************
 *
 * @ingroup dplasma_complex64
 *
 *  dplasma_ztrsmpl_Destruct - Free the data structure associated to an taskpool
 *  created with dplasma_ztrsmpl_New().
 *
 *******************************************************************************
 *
 * @param[in,out] taskpool
 *          On entry, the taskpool to destroy.
 *          On exit, the taskpool cannot be used anymore.
 *
 *******************************************************************************
 *
 * @sa dplasma_ztrsmpl_New
 * @sa dplasma_ztrsmpl
 *
 ******************************************************************************/
void
dplasma_ztrsmpl_Destruct( parsec_taskpool_t *tp )
{
    parsec_ztrsmpl_taskpool_t *parsec_trsmpl = (parsec_ztrsmpl_taskpool_t *)tp;

    parsec_matrix_del2arena( parsec_trsmpl->arenas[PARSEC_ztrsmpl_DEFAULT_ARENA] );
    parsec_matrix_del2arena( parsec_trsmpl->arenas[PARSEC_ztrsmpl_PIVOT_ARENA  ] );
    parsec_matrix_del2arena( parsec_trsmpl->arenas[PARSEC_ztrsmpl_SMALL_L_ARENA] );

    parsec_taskpool_free(tp);
}

/**
 *******************************************************************************
 *
 * @ingroup dplasma_complex64
 *
 * dplasma_ztrsmpl - Solves U*x = b, when U has been generated through LU
 * factorization with incremental pivoting strategy
 * See dplasma_zgetrf_incpiv().
 *
 *******************************************************************************
 *
 * @param[in,out] parsec
 *          The parsec context of the application that will run the operation.
 *
 * @param[in] A
 *          Descriptor of the distributed matrix A to be factorized.
 *          On entry, The factorized matrix through dplasma_zgetrf_incpiv_New()
 *          routine.  Elements on and above the diagonal are the elements of
 *          U. Elements below the diagonal are NOT the classic L, but the L
 *          factors obtaines by succesive pivoting.
 *
 * @param[in] L
 *          Descriptor of the matrix L distributed exactly as the A matrix.
 *           - If IPIV != NULL, L.mb defines the IB parameter of the tile LU
 *          algorithm. This matrix must be of size A.mt * L.mb - by - A.nt *
 *          L.nb, with L.nb == A.nb.
 *          On entry, contains auxiliary information required to solve the
 *          system and generated by dplasma_zgetrf_inciv_New().
 *           - If IPIV == NULL, pivoting information are stored within
 *          L. (L.mb-1) defines the IB parameter of the tile LU algorithm. This
 *          matrix must be of size A.mt * L.mb - by - A.nt * L.nb, with L.nb =
 *          A.nb, and L.mb = ib+1.
 *          The first A.mb elements contains the IPIV information, the leftover
 *          contains auxiliary information required to solve the system.
 *
 * @param[in] IPIV
 *          Descriptor of the IPIV matrix. Should be distributed exactly as the
 *          A matrix. This matrix must be of size A.m - by - A.nt with IPIV.mb =
 *          A.mb and IPIV.nb = 1.
 *          On entry, contains the pivot indices of the successive row
 *          interchanged performed during the factorization.
 *          If IPIV == NULL, rows interchange information is stored within L.
 *
 * @param[in,out] B
 *          On entry, the N-by-NRHS right hand side matrix B.
 *          On exit, if return value = 0, B is overwritten by the solution matrix X.
 *
 *******************************************************************************
 *
 * @return
 *          \retval -i if the ith parameters is incorrect.
 *          \retval 0 on success.
 *          \retval i if ith value is singular. Result is incoherent.
 *
 *******************************************************************************
 *
 * @sa dplasma_ztrsmpl
 * @sa dplasma_ztrsmpl_Destruct
 * @sa dplasma_ctrsmpl_New
 * @sa dplasma_dtrsmpl_New
 * @sa dplasma_strsmpl_New
 *
 ******************************************************************************/
int
dplasma_ztrsmpl( parsec_context_t *parsec,
                 const parsec_tiled_matrix_dc_t *A,
                 const parsec_tiled_matrix_dc_t *L,
                 const parsec_tiled_matrix_dc_t *IPIV,
                       parsec_tiled_matrix_dc_t *B )
{
    parsec_taskpool_t *parsec_ztrsmpl = NULL;

    if ( (A->mt != L->mt) || (A->nt != L->nt) ) {
        dplasma_error("dplasma_ztrsmpl", "L doesn't have the same number of tiles as A");
        return -3;
    }
    if ( (IPIV != NULL) && ((A->mt != IPIV->mt) || (A->nt != IPIV->nt)) ) {
        dplasma_error("dplasma_ztrsmpl", "IPIV doesn't have the same number of tiles as A");
        return -4;
    }

    parsec_ztrsmpl = dplasma_ztrsmpl_New(A, L, IPIV, B);
    if ( parsec_ztrsmpl != NULL )
    {
        parsec_context_add_taskpool( parsec, parsec_ztrsmpl );
        dplasma_wait_until_completion( parsec );
        dplasma_ztrsmpl_Destruct( parsec_ztrsmpl );
        return 0;
    }
    else
        return -101;
}
