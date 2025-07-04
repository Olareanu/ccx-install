/*
 * Copyright (c) 2010-2019 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 *
 * @precisions normal z -> s d c
 *
 */

#include "dplasma.h"
#include "dplasma/types.h"
#include "dplasma/lib/dplasmaaux.h"
#include "parsec/private_mempool.h"

#include "zunmlq_param_LN.h"
#include "zunmlq_param_LC.h"
#include "zunmlq_param_RN.h"
#include "zunmlq_param_RC.h"

/**
 *******************************************************************************
 *
 * @ingroup dplasma_complex64
 *
 *  dplasma_zunmlq_param_New - Generates the parsec taskpool that overwrites the
 *  general M-by-N matrix C with
 *
 *                  SIDE = 'L'     SIDE = 'R'
 *  TRANS = 'N':      Q * C          C * Q
 *  TRANS = 'C':      Q**H * C       C * Q**H
 *
 *  where Q is a unitary matrix defined as the product of k elementary
 *  reflectors
 *
 *        Q = H(1) H(2) . . . H(k)
 *
 *  as returned by dplasma_zgelqf_param(). Q is of order M if side = PlasmaLeft
 *  and of order N if side = PlasmaRight.
 *
 * WARNING: The computations are not done by this call.
 *
 *******************************************************************************
 *
 * @param[in] side
 *          @arg PlasmaLeft:  apply Q or Q**H from the left;
 *          @arg PlasmaRight: apply Q or Q**H from the right.
 *
 * @param[in] trans
 *          @arg PlasmaNoTrans:   no transpose, apply Q;
 *          @arg PlasmaConjTrans: conjugate transpose, apply Q**H.
 *
 * @param[in] qrtree
 *          The structure that describes the trees used to perform the
 *          hierarchical QR factorization.
 *          See dplasma_hqr_init() or dplasma_systolic_init().
 *
 * @param[in] A
 *          Descriptor of the matrix A of size M-by-K factorized with the
 *          dplasma_zgelqf_param_New() routine.
 *          On entry, the i-th row must contain the vector which defines the
 *          elementary reflector H(i), for i = 1,2,...,k, as returned by
 *          dplasma_zgelqf_param_New_New() in the first k rows of its array
 *          argument A.
 *          If side == PlasmaLeft,  M >= K >= 0.
 *          If side == PlasmaRight, N >= K >= 0.
 *
 * @param[in] TS
 *          Descriptor of the matrix TS distributed exactly as the A
 *          matrix. TS.mb defines the IB parameter of tile LQ algorithm. This
 *          matrix must be of size A.mt * TS.mb - by - A.nt * TS.nb, with TS.nb
 *          == A.nb.  This matrix is initialized during the call to
 *          dplasma_zgelqf_param_New().
 *
 * @param[in] TT
 *          Descriptor of the matrix TT distributed exactly as the A
 *          matrix. TT.mb defines the IB parameter of tile LQ algorithm. This
 *          matrix must be of size A.mt * TT.mb - by - A.nt * TT.nb, with TT.nb
 *          == A.nb.  This matrix is initialized during the call to
 *          dplasma_zgelqf_param_New().
 *
 * @param[in,out] C
 *          Descriptor of the M-by-N matrix C.
 *          On exit, the matrix C is overwritten by the result.
 *
 *******************************************************************************
 *
 * @return
 *          \retval NULL if incorrect parameters are given.
 *          \retval The parsec taskpool describing the operation that can be
 *          enqueued in the runtime with parsec_context_add_taskpool(). It, then, needs to be
 *          destroy with dplasma_zunmlq_param_Destruct();
 *
 *******************************************************************************
 *
 * @sa dplasma_zunmlq_param_Destruct
 * @sa dplasma_zunmlq_param
 * @sa dplasma_cunmlq_param_New
 * @sa dplasma_dormlq_param_New
 * @sa dplasma_sormlq_param_New
 * @sa dplasma_zgelqf_param_New
 *
 ******************************************************************************/
parsec_taskpool_t*
dplasma_zunmlq_param_New( PLASMA_enum side, PLASMA_enum trans,
                          dplasma_qrtree_t *qrtree,
                          parsec_tiled_matrix_dc_t *A,
                          parsec_tiled_matrix_dc_t *TS,
                          parsec_tiled_matrix_dc_t *TT,
                          parsec_tiled_matrix_dc_t *C)
{
    parsec_taskpool_t* tp = NULL;
    int An, ib = TS->mb;

    /* if ( !dplasma_check_desc(A) ) { */
    /*     dplasma_error("dplasma_zunmlq_param_New", "illegal A descriptor"); */
    /*     return NULL; */
    /* } */
    /* if ( !dplasma_check_desc(T) ) { */
    /*     dplasma_error("dplasma_zunmlq_param_New", "illegal T descriptor"); */
    /*     return NULL; */
    /* } */
    /* if ( !dplasma_check_desc(C) ) { */
    /*     dplasma_error("dplasma_zunmlq_param_New", "illegal C descriptor"); */
    /*     return NULL; */
    /* } */
    if ((side != PlasmaLeft) && (side != PlasmaRight)) {
        dplasma_error("dplasma_zunmlq_param_New", "illegal value of side");
        return NULL;
    }
    if ((trans != PlasmaNoTrans) &&
        (trans != PlasmaTrans)   &&
        (trans != PlasmaConjTrans)) {
        dplasma_error("dplasma_zunmlq_param_New", "illegal value of trans");
        return NULL;
    }

    if ( side == PlasmaLeft ) {
        An = C->m;
    } else {
        An = C->n;
    }

    if ( A->m > An ) {
        dplasma_error("dplasma_zunmlq_param_New", "illegal value of A->m");
        return NULL;
    }
    if ( A->n != An ) {
        dplasma_error("dplasma_zunmlq_param_New", "illegal value of A->n");
        return NULL;
    }
    if ( (TS->nt != A->nt) || (TS->mt != A->mt) ) {
        dplasma_error("dplasma_zunmlq_param_New", "illegal size of TS (TS should have as many tiles as A)");
        return NULL;
    }
    if ( (TT->nt != A->nt) || (TT->mt != A->mt) ) {
        dplasma_error("dplasma_zunmlq_param_New", "illegal size of TT (TT should have as many tiles as A)");
        return NULL;
    }

    if ( side == PlasmaLeft ) {
        if ( trans == PlasmaNoTrans ) {
            tp = (parsec_taskpool_t*)parsec_zunmlq_param_LN_new( side, trans,
                                                                 A,
                                                                 C,
                                                                 TS,
                                                                 TT,
                                                                 *qrtree,
                                                                 NULL);
        } else {
            tp = (parsec_taskpool_t*)parsec_zunmlq_param_LC_new( side, trans,
                                                                 A,
                                                                 C,
                                                                 TS,
                                                                 TT,
                                                                 *qrtree,
                                                                 NULL);
        }
    } else {
        if ( trans == PlasmaNoTrans ) {
            tp = (parsec_taskpool_t*)parsec_zunmlq_param_RN_new( side, trans,
                                                                 A,
                                                                 C,
                                                                 TS,
                                                                 TT,
                                                                 *qrtree,
                                                                 NULL);
        } else {
            tp = (parsec_taskpool_t*)parsec_zunmlq_param_RC_new( side, trans,
                                                                 A,
                                                                 C,
                                                                 TS,
                                                                 TT,
                                                                 *qrtree,
                                                                 NULL);
        }
    }

    ((parsec_zunmlq_param_LC_taskpool_t*)tp)->_g_p_work = (parsec_memory_pool_t*)malloc(sizeof(parsec_memory_pool_t));
    parsec_private_memory_init( ((parsec_zunmlq_param_LC_taskpool_t*)tp)->_g_p_work, ib * TS->nb * sizeof(parsec_complex64_t) );

    /* Default type */
    dplasma_add2arena_tile( ((parsec_zunmlq_param_LC_taskpool_t*)tp)->arenas[PARSEC_zunmlq_param_LC_DEFAULT_ARENA],
                            A->mb*A->nb*sizeof(parsec_complex64_t),
                            PARSEC_ARENA_ALIGNMENT_SSE,
                            parsec_datatype_double_complex_t, A->mb );

    /* Lower triangular part of tile without diagonal */
    dplasma_add2arena_lower( ((parsec_zunmlq_param_LC_taskpool_t*)tp)->arenas[PARSEC_zunmlq_param_LC_LOWER_TILE_ARENA],
                             A->mb*A->nb*sizeof(parsec_complex64_t),
                             PARSEC_ARENA_ALIGNMENT_SSE,
                             parsec_datatype_double_complex_t, A->mb, 1 );

    /* Upper triangular part of tile with diagonal */
    dplasma_add2arena_upper( ((parsec_zunmlq_param_LC_taskpool_t*)tp)->arenas[PARSEC_zunmlq_param_LC_UPPER_TILE_ARENA],
                             A->mb*A->nb*sizeof(parsec_complex64_t),
                             PARSEC_ARENA_ALIGNMENT_SSE,
                             parsec_datatype_double_complex_t, A->mb, 0 );

    /* Little T */
    dplasma_add2arena_rectangle( ((parsec_zunmlq_param_LC_taskpool_t*)tp)->arenas[PARSEC_zunmlq_param_LC_LITTLE_T_ARENA],
                                 TS->mb*TS->nb*sizeof(parsec_complex64_t),
                                 PARSEC_ARENA_ALIGNMENT_SSE,
                                 parsec_datatype_double_complex_t, TS->mb, TS->nb, -1);

    return tp;
}

/**
 *******************************************************************************
 *
 * @ingroup dplasma_complex64
 *
 *  dplasma_zunmlq_param_Destruct - Free the data structure associated to an taskpool
 *  created with dplasma_zunmlq_param_New().
 *
 *******************************************************************************
 *
 * @param[in,out] taskpool
 *          On entry, the taskpool to destroy.
 *          On exit, the taskpool cannot be used anymore.
 *
 *******************************************************************************
 *
 * @sa dplasma_zunmlq_param_New
 * @sa dplasma_zunmlq_param
 *
 ******************************************************************************/
void
dplasma_zunmlq_param_Destruct( parsec_taskpool_t *tp )
{
    parsec_zunmlq_param_LC_taskpool_t *parsec_zunmlq_param = (parsec_zunmlq_param_LC_taskpool_t *)tp;

    parsec_matrix_del2arena( parsec_zunmlq_param->arenas[PARSEC_zunmlq_param_LC_LOWER_TILE_ARENA] );
    parsec_matrix_del2arena( parsec_zunmlq_param->arenas[PARSEC_zunmlq_param_LC_LITTLE_T_ARENA  ] );
    parsec_matrix_del2arena( parsec_zunmlq_param->arenas[PARSEC_zunmlq_param_LC_DEFAULT_ARENA   ] );
    parsec_matrix_del2arena( parsec_zunmlq_param->arenas[PARSEC_zunmlq_param_LC_UPPER_TILE_ARENA] );

    parsec_private_memory_fini( parsec_zunmlq_param->_g_p_work );
    free( parsec_zunmlq_param->_g_p_work );

    parsec_taskpool_free(tp);
}

/**
 *******************************************************************************
 *
 * @ingroup dplasma_complex64
 *
 *  dplasma_zunmlq_param - Generates the parsec taskpool that overwrites the general
 *  M-by-N matrix C with
 *
 *                  SIDE = 'L'     SIDE = 'R'
 *  TRANS = 'N':      Q * C          C * Q
 *  TRANS = 'C':      Q**H * C       C * Q**H
 *
 *  where Q is a unitary matrix defined as the product of k elementary
 *  reflectors
 *
 *        Q = H(1) H(2) . . . H(k)
 *
 *  as returned by dplasma_zgelqf_param(). Q is of order M if side = PlasmaLeft
 *  and of order N if side = PlasmaRight.
 *
 *******************************************************************************
 *
 * @param[in,out] parsec
 *          The parsec context of the application that will run the operation.
 *
 * @param[in] side
 *          @arg PlasmaLeft:  apply Q or Q**H from the left;
 *          @arg PlasmaRight: apply Q or Q**H from the right.
 *
 * @param[in] trans
 *          @arg PlasmaNoTrans:   no transpose, apply Q;
 *          @arg PlasmaConjTrans: conjugate transpose, apply Q**H.
 *
 * @param[in] qrtree
 *          The structure that describes the trees used to perform the
 *          hierarchical QR factorization.
 *          See dplasma_hqr_init() or dplasma_systolic_init().
 *
 * @param[in] qrtree
 *          The structure that describes the trees used to perform the
 *          hierarchical QR factorization.
 *          See dplasma_hqr_init() or dplasma_systolic_init().
 *
 * @param[in] A
 *          Descriptor of the matrix A of size M-by-K factorized with the
 *          dplasma_zgelqf_New() routine.
 *          On entry, the i-th row must contain the vector which
 *          defines the elementary reflector H(i), for i = 1,2,...,k, as
 *          returned by dplasma_zgelqf_New() in the first k rows of its array
 *          argument A.
 *          If side == PlasmaLeft,  M >= K >= 0.
 *          If side == PlasmaRight, N >= K >= 0.
 *
 * @param[in] TS
 *          Descriptor of the matrix TS distributed exactly as the A
 *          matrix. TS.mb defines the IB parameter of tile LQ algorithm. This
 *          matrix must be of size A.mt * TS.mb - by - A.nt * TS.nb, with TS.nb
 *          == A.nb.  This matrix is initialized during the call to
 *          dplasma_zgelqf_param_New().
 *
 * @param[in] TT
 *          Descriptor of the matrix TT distributed exactly as the A
 *          matrix. TT.mb defines the IB parameter of tile LQ algorithm. This
 *          matrix must be of size A.mt * TT.mb - by - A.nt * TT.nb, with TT.nb
 *          == A.nb.  This matrix is initialized during the call to
 *          dplasma_zgelqf_param_New().
 *
 * @param[in,out] C
 *          Descriptor of the M-by-N matrix C.
 *          On exit, the matrix C is overwritten by the result.
 *
 *******************************************************************************
 *
 * @return
 *          \retval -i if the ith parameters is incorrect.
 *          \retval 0 on success.
 *
 *******************************************************************************
 *
 * @sa dplasma_zunmlq_param_New
 * @sa dplasma_zunmlq_param_Destruct
 * @sa dplasma_cunmlq_param
 * @sa dplasma_dormlq_param
 * @sa dplasma_sormlq_param
 * @sa dplasma_zgelqf_param
 *
 ******************************************************************************/
int
dplasma_zunmlq_param( parsec_context_t *parsec,
                      PLASMA_enum side, PLASMA_enum trans,
                      dplasma_qrtree_t    *qrtree,
                      parsec_tiled_matrix_dc_t *A,
                      parsec_tiled_matrix_dc_t *TS,
                      parsec_tiled_matrix_dc_t *TT,
                      parsec_tiled_matrix_dc_t *C )
{
    parsec_taskpool_t *parsec_zunmlq_param = NULL;
    int An;

    if (parsec == NULL) {
        dplasma_error("dplasma_zunmlq_param", "dplasma not initialized");
        return -1;
    }

    if ((side != PlasmaLeft) && (side != PlasmaRight)) {
        dplasma_error("dplasma_zunmlq_param", "illegal value of side");
        return -1;
    }
    if ((trans != PlasmaNoTrans) &&
        (trans != PlasmaTrans)   &&
        (trans != PlasmaConjTrans)) {
        dplasma_error("dplasma_zunmlq_param", "illegal value of trans");
        return -2;
    }

    if ( side == PlasmaLeft ) {
        An = C->m;
    } else {
        An = C->n;
    }
    if ( A->n != An ) {
        dplasma_error("dplasma_zunmlq_param", "illegal value of A->n");
        return -3;
    }
    if ( A->m > An ) {
        dplasma_error("dplasma_zunmlq_param", "illegal value of A->m");
        return -5;
    }
    if ( (TS->nt != A->nt) || (TS->mt != A->mt) ) {
        dplasma_error("dplasma_zunmlq_param", "illegal size of TS (TS should have as many tiles as A)");
        return -20;
    }
    if ( (TT->nt != A->nt) || (TT->mt != A->mt) ) {
        dplasma_error("dplasma_zunmlq_param", "illegal size of TT (TT should have as many tiles as A)");
        return -20;
    }

    if (dplasma_imin(C->m, dplasma_imin(C->n, A->m)) == 0)
        return 0;

    parsec_zunmlq_param = dplasma_zunmlq_param_New(side, trans, qrtree, A, TS, TT, C);

    if ( parsec_zunmlq_param != NULL ){
        parsec_context_add_taskpool(parsec, (parsec_taskpool_t*)parsec_zunmlq_param);
        dplasma_wait_until_completion(parsec);
        dplasma_zunmlq_param_Destruct( parsec_zunmlq_param );
    }

    return 0;
}
