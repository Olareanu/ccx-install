/**
 * Copyright (c) 2013-2017 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 */

/* **************************************************************************** */
/**
 * @file insert_function.c
 *
 * @version 2.0.0
 *
 */

/* Define a group for Doxygen documentation */
/**
 * @defgroup DTD_INTERFACE Dynamic Task Discovery interface for PaRSEC
 * @ingroup parsec_public
 *
 * These functions are available from the PaRSEC library for the
 * scheduling of kernel routines.
 */

/* Define a group for Doxygen documentation */
/**
 * @defgroup DTD_INTERFACE_INTERNAL Dynamic Task Discovery functions for PaRSEC
 * @ingroup parsec_internal
 *
 * These functions are not available from the PaRSEC library for the
 * scheduling of kernel routines.
 */

#include <stdlib.h>
#include <sys/time.h>

#include "parsec/parsec_config.h"
#include "parsec/parsec_internal.h"
#include "parsec/scheduling.h"
#include "parsec/remote_dep.h"
#include "parsec/devices/device.h"
#include "parsec/constants.h"
#include "parsec/vpmap.h"
#include "parsec/utils/mca_param.h"
#include "parsec/mca/sched/sched.h"
#include "parsec/interfaces/interface.h"
#include "parsec/interfaces/superscalar/insert_function_internal.h"
#include "parsec/parsec_prof_grapher.h"
#include "parsec/mca/pins/pins.h"
#include "parsec/data_dist/matrix/matrix.h"
#include "parsec/utils/debug.h"
#include "parsec/data_distribution.h"

/* This allows DTD to have a separate stream for debug verbose output */
int parsec_dtd_debug_output;
static int parsec_dtd_debug_verbose = -1;

static int parsec_dtd_profile_verbose = 0;

static parsec_dc_key_t parsec_dtd_dc_id = 0;
int32_t __parsec_dtd_is_initialized     = 0; /**< Indicates init of dtd environment is completed */

int parsec_dtd_window_size             = 8000;   /**< Default window size */
int parsec_dtd_threshold_size          = 4000;   /**< Default threshold size of tasks for master thread to wait on */
static int parsec_dtd_task_hash_table_size = 1<<10; /**< Default task hash table size */
static int parsec_dtd_tile_hash_table_size = 1<<10; /**< Default tile hash table size */
static int parsec_dtd_no_of_arenas = 16;

int parsec_dtd_dump_traversal_info = 60; /**< Level for printing traversal info */
int parsec_dtd_dump_function_info  = 50; /**< Level for printing function_structure info */
int insert_task_trace_keyin;
int insert_task_trace_keyout;
int hashtable_trace_keyin;
int hashtable_trace_keyout;

parsec_arena_t **parsec_dtd_arenas;

extern parsec_sched_module_t *current_scheduler;

/* Global mempool for all the parsec DTD taskpools that will be created for a run */
parsec_mempool_t *parsec_dtd_taskpool_mempool = NULL;

/* Global mempool for all tiles */
parsec_mempool_t *parsec_dtd_tile_mempool = NULL;

/**
 * All the static functions should be declared before being defined.
 */
static int
hook_of_dtd_task(parsec_execution_stream_t *es,
                 parsec_task_t *this_task);

static void
parsec_dtd_iterate_successors(parsec_execution_stream_t *es,
                              const parsec_task_t *this_task,
                              uint32_t action_mask,
                              parsec_ontask_function_t *ontask,
                              void *ontask_arg);

static int
parsec_dtd_release_deps(parsec_execution_stream_t *,
                        parsec_task_t *,
                        uint32_t, parsec_remote_deps_t *);


static parsec_hook_return_t
complete_hook_of_dtd(parsec_execution_stream_t *,
                     parsec_task_t *);

static parsec_key_fn_t DTD_key_fns = {
    .key_equal = parsec_hash_table_generic_64bits_key_equal,
    .key_print = parsec_hash_table_generic_64bits_key_print,
    .key_hash  = parsec_hash_table_generic_64bits_key_hash
};

inline int parsec_dtd_task_is_local(parsec_dtd_task_t *task) { return task->rank == task->super.taskpool->context->my_rank;}
inline int parsec_dtd_task_is_remote(parsec_dtd_task_t *task) { return !parsec_dtd_task_is_local(task);}

/* Copied from parsec/scheduling.c, will need to be exposed */
#define TIME_STEP 5410
#define MIN(x, y) ( (x)<(y)?(x):(y) )
static inline unsigned long exponential_backoff(uint64_t k)
{
    unsigned int n = MIN( 64, k );
    unsigned int r = (unsigned int) ((double)n * ((double)rand()/(double)RAND_MAX));
    return r * TIME_STEP;
}

void
parsec_detach_all_dtd_taskpool_from_context( parsec_context_t *context )
{
    /* Here we wait on all dtd taskpool registered with us */
    if( NULL != context->taskpool_array && 0 < context->taskpool_array_occupied ) {
        int iterator;
        for( iterator = 0; iterator < context->taskpool_array_occupied; iterator++ ) {
            parsec_dtd_taskpool_t *tp = (parsec_dtd_taskpool_t *)context->taskpool_array[iterator];
            if( NULL != tp ) {
                if( tp->enqueue_flag ) {
                    parsec_taskpool_update_runtime_nbtask( (parsec_taskpool_t *)tp, -1 );
                }
                context->taskpool_array[iterator] = NULL;
            }
        }
        context->taskpool_array_occupied = 0;
    }
}

void
parsec_dtd_attach_taskpool_to_context( parsec_taskpool_t  *tp,
                                       parsec_context_t *parsec_context )
{
    if( (NULL == parsec_context->taskpool_array) ||
        (parsec_context->taskpool_array_occupied >= parsec_context->taskpool_array_size) ) {
        parsec_context->taskpool_array_size <<= 1;
        parsec_context->taskpool_array = (parsec_taskpool_t**)realloc(parsec_context->taskpool_array, parsec_context->taskpool_array_size * sizeof(parsec_taskpool_t*) );
        /* NULLify all the new elements */
        for( int32_t i = (parsec_context->taskpool_array_size>>1); i < parsec_context->taskpool_array_size;
             parsec_context->taskpool_array[i++] = NULL );
    }
    parsec_context->taskpool_array[parsec_context->taskpool_array_occupied++] = tp;
}

/* enqueue wrapper for dtd */
int
parsec_dtd_enqueue_taskpool( parsec_taskpool_t *tp, void *parsec_context )
{
    (void)parsec_context;

    parsec_dtd_taskpool_t *dtd_tp = (parsec_dtd_taskpool_t *)tp;
    dtd_tp->super.nb_pending_actions = 1;  /* For the future tasks that will be inserted */
    dtd_tp->super.nb_tasks -= PARSEC_RUNTIME_RESERVED_NB_TASKS;
    dtd_tp->super.nb_tasks += 1;  /* For the bounded window, starting with +1 task */
    dtd_tp->enqueue_flag    = 1;

    parsec_dtd_taskpool_retain(tp);

    parsec_taskpool_enable(tp, NULL, NULL, NULL,
                          !!(tp->context->nb_nodes > 1));

    /* Attaching the reference of this taskpool to the parsec context */
    parsec_dtd_attach_taskpool_to_context( tp, tp->context );
    return 0;
}

/* To create object of class parsec_dtd_task_t that inherits parsec_task_t
 * class
 */
OBJ_CLASS_INSTANCE(parsec_dtd_task_t, parsec_task_t,
                   NULL, NULL);

/* To create object of class .list_itemdtd_tile_t that inherits parsec_list_item_t
 * class
 */
OBJ_CLASS_INSTANCE(parsec_dtd_tile_t, parsec_list_item_t,
                   NULL, NULL);

/* To create object of class parsec_taskpool_t that inherits parsec_list_t
 * class
 */
OBJ_CLASS_INSTANCE(parsec_taskpool_t, parsec_list_item_t,
                   NULL, NULL);

/***************************************************************************//**
 *
 * Constructor of PaRSEC's DTD taskpool.
 *
 * @param[in,out]   tp
 *                      Pointer to taskpool which will be constructed
 *
 * @ingroup         DTD_INTERFACE_INTERNAL
 *
 ******************************************************************************/
void parsec_dtd_taskpool_constructor(parsec_dtd_taskpool_t *tp)
{
    int nb;
    tp->startup_list = (parsec_task_t**)calloc( vpmap_get_nb_vp(), sizeof(parsec_task_t*));

    tp->function_counter = 0;

    tp->two_hash_table = (parsec_dtd_two_hash_table_t *) malloc(sizeof(parsec_dtd_two_hash_table_t));
    parsec_atomic_unlock(&tp->two_hash_table->atomic_lock);

    tp->two_hash_table->task_and_rem_dep_h_table = OBJ_NEW(parsec_hash_table_t);
    for(nb = 1; nb < 16 && (1<<nb)<parsec_dtd_task_hash_table_size; nb++) /* nothing */;
    parsec_hash_table_init(tp->two_hash_table->task_and_rem_dep_h_table,
                           offsetof(dtd_hash_table_pointer_item_t, ht_item),
                           nb,
                           DTD_key_fns,
                           tp->two_hash_table->task_and_rem_dep_h_table);

    tp->function_h_table = OBJ_NEW(parsec_hash_table_t);
    for(nb = 1; nb < 16 && (1<<nb)<PARSEC_DTD_NB_TASK_CLASSES; nb++) /* nothing */;
    parsec_hash_table_init(tp->function_h_table,
                           offsetof(dtd_hash_table_pointer_item_t, ht_item),
                           nb,
                           DTD_key_fns,
                           tp->function_h_table);

    tp->super.startup_hook    = parsec_dtd_startup;
    tp->super.destructor      = (parsec_destruct_fn_t)parsec_dtd_taskpool_destruct;
    tp->super.task_classes_array = (const parsec_task_class_t **) malloc( PARSEC_DTD_NB_TASK_CLASSES * sizeof(parsec_task_class_t *));

    for( int i = 0; i < PARSEC_DTD_NB_TASK_CLASSES; i++ ) {
        tp->super.task_classes_array[i] = NULL;
    }

    tp->super.dependencies_array  = calloc(PARSEC_DTD_NB_TASK_CLASSES, sizeof(parsec_dependencies_t *));

#if defined(PARSEC_PROF_TRACE)
    tp->super.profiling_array     = calloc (2 * PARSEC_DTD_NB_TASK_CLASSES, sizeof(int));
#endif /* defined(PARSEC_PROF_TRACE) */

    /* Initializing hash_table_bucket mempool */
    tp->hash_table_bucket_mempool = (parsec_mempool_t*) malloc (sizeof(parsec_mempool_t));
    parsec_mempool_construct( tp->hash_table_bucket_mempool,
                              NULL, sizeof(dtd_hash_table_pointer_item_t),
                              offsetof(dtd_hash_table_pointer_item_t, mempool_owner),
                              1/* no. of threads*/ );
}

/***************************************************************************//**
 *
 * Destructor of PaRSEC's DTD taskpool.
 *
 * @param[in,out]   tp
 *                      Pointer to taskpool which will be destroyed
 *
 * @ingroup         DTD_INTERFACE_INTERNAL
 *
 ******************************************************************************/
void
parsec_dtd_taskpool_destructor(parsec_dtd_taskpool_t *tp)
{
    uint32_t i;
#if defined(PARSEC_PROF_TRACE)
    free((void *)tp->super.profiling_array);
#endif /* defined(PARSEC_PROF_TRACE) */

    /* Destroy the data repositories for this object */
    for (i = 0; i < PARSEC_DTD_NB_TASK_CLASSES; i++) {
        parsec_destruct_dependencies(tp->super.dependencies_array[i]);
        tp->super.dependencies_array[i] = NULL;
    }

    free(tp->super.dependencies_array);
    tp->super.dependencies_array = NULL;

    /* Unregister the taskpool from the devices */
    for (i = 0; i < parsec_nb_devices; i++) {
        parsec_device_t *device = parsec_devices_get(i);
        if( !(tp->super.devices_index_mask & (1 << device->device_index)) )
            continue;
        tp->super.devices_index_mask &= ~(1 << device->device_index);
        if ((NULL == device) || (NULL == device->device_taskpool_unregister))
            continue;
        if( NULL != device->device_taskpool_unregister )
            (void)device->device_taskpool_unregister(device, &tp->super);
    }
    assert( 0 == tp->super.devices_index_mask );
    free(tp->super.task_classes_array);

    /* dtd_taskpool specific */
    parsec_mempool_destruct(tp->hash_table_bucket_mempool);
    free(tp->hash_table_bucket_mempool);
    free(tp->startup_list);

    parsec_hash_table_fini(tp->two_hash_table->task_and_rem_dep_h_table);
    OBJ_RELEASE(tp->two_hash_table->task_and_rem_dep_h_table);
    free(tp->two_hash_table);

    parsec_hash_table_fini(tp->function_h_table);
    OBJ_RELEASE(tp->function_h_table);
}

/* To create object of class parsec_dtd_taskpool_t that inherits parsec_taskpool_t
 * class
 */
OBJ_CLASS_INSTANCE(parsec_dtd_taskpool_t, parsec_taskpool_t,
                   parsec_dtd_taskpool_constructor, parsec_dtd_taskpool_destructor);


/* **************************************************************************** */
/**
 * Init function of Dynamic Task Discovery Interface. This function should never
 * be called directly, it will be automatically called upon creation of the
 * first taskpool. The corresponding finalization function (parsec_dtd_fini)
 * will then be called once all references to the DTD will dissapear.
 *
 * Here a global(per node/process) taskpool mempool for PaRSEC's DTD taskpool
 * is constructed. The mca_params passed to the runtime are also scanned
 * and set here.
 * List of mca options available for DTD interface are:
 *  - dtd_traversal_info (default=0 off):   This prints the DAG travesal
 *                                          info for each node in the DAG.
 *  - dtd_function_info (default=0 off):    This prints the DOT compliant
 *                                          output to check the relationship
 *                                          between the master structure,
 *                                          which represent each task class.
 *  - dtd_tile_hash_size (default=104729):  This sets the tile hash table
 *                                          size.
 *  - dtd_task_hash_size (default=11):      This sets the size of task hash
 *                                          table.
 *  - parsec_dtd_window_size (default:2048):       To set the window size for the
 *                                          execution.
 *  - parsec_dtd_threshold_size (default:2048):    This sets the threshold task
 *                                          size up to which the master
 *                                          thread will wait before going
 *                                          back and inserting task into the
 *                                          engine.
 * @ingroup DTD_INTERFACE
 */
static void
parsec_dtd_lazy_init(void)
{
    parsec_dtd_taskpool_t  *tp;

    (void)parsec_mca_param_reg_int_name("dtd", "debug_verbose",
                                        "This param indicates the vebosity level of separate dtd output stream and "
                                        "also determines if we will be using a separate output stream for DTD or not\n"
                                        "Level 50 will print relationship between task class\n"
                                        "Level 60 will print level 50 + traversal of the DAG",
                                        false, false, parsec_dtd_debug_verbose,
                                        &parsec_dtd_debug_verbose);

    /* Registering mca param for tile hash table size */
    (void)parsec_mca_param_reg_int_name("dtd", "tile_hash_size",
                                       "Registers the supplied size overriding the default size of tile hash table",
                                       false, false, parsec_dtd_tile_hash_table_size, &parsec_dtd_tile_hash_table_size);

    /* Registering mca param for task hash table size */
    (void)parsec_mca_param_reg_int_name("dtd", "task_hash_size",
                                       "Registers the supplied size overriding the default size of task hash table",
                                       false, false, parsec_dtd_task_hash_table_size, &parsec_dtd_task_hash_table_size);

    /* Registering mca param for window size */
    (void)parsec_mca_param_reg_int_name("dtd", "window_size",
                                       "Registers the supplied size overriding the default size of window size",
                                       false, false, parsec_dtd_window_size, &parsec_dtd_window_size);

    /* Registering mca param for threshold size */
    (void)parsec_mca_param_reg_int_name("dtd", "threshold_size",
                                       "Registers the supplied size overriding the default size of threshold size",
                                       false, false, parsec_dtd_threshold_size, &parsec_dtd_threshold_size);

    /* Registering mca param for threshold size */
    (void)parsec_mca_param_reg_int_name("dtd", "profile_verbose",
                                       "This param turns events that profiles task insertion and other dtd overheads",
                                       false, false, parsec_dtd_profile_verbose, &parsec_dtd_profile_verbose);


    /* Register separate dtd_debug_output_stream */
    if(-1 != parsec_dtd_debug_verbose) {
        /* By default we use parsec_debug_output,
         * if it is indicated otherwise, we use a separate
         * stream to output dtd verbose debug information
         */
        parsec_dtd_debug_output = parsec_output_open(NULL);
        /* We will have only two level of verbosity
         * 1. For traversal info of the DAG - level 49
         * 2. Level 1 + relationship between task classes - leve 50
         */
        parsec_output_set_verbosity(parsec_dtd_debug_output, parsec_dtd_debug_verbose);
    } else {
        /* Falling back to default output stream */
        parsec_dtd_debug_output = parsec_debug_output;
    }

    parsec_dtd_taskpool_mempool = (parsec_mempool_t*) malloc (sizeof(parsec_mempool_t));
    parsec_mempool_construct( parsec_dtd_taskpool_mempool,
                              OBJ_CLASS(parsec_dtd_taskpool_t), sizeof(parsec_dtd_taskpool_t),
                              offsetof(parsec_dtd_taskpool_t, mempool_owner),
                              1/* no. of threads*/ );

    tp = (parsec_dtd_taskpool_t *)parsec_thread_mempool_allocate(parsec_dtd_taskpool_mempool->thread_mempools);
    parsec_thread_mempool_free( parsec_dtd_taskpool_mempool->thread_mempools, tp );

    /* Initializing the tile mempool and attaching it to the tp */
    parsec_dtd_tile_mempool = (parsec_mempool_t*) malloc (sizeof(parsec_mempool_t));
    parsec_mempool_construct( parsec_dtd_tile_mempool,
                              OBJ_CLASS(parsec_dtd_tile_t), sizeof(parsec_dtd_tile_t),
                              offsetof(parsec_dtd_tile_t, mempool_owner),
                              1/* no. of threads*/ );

    int i;
    parsec_dtd_arenas = (parsec_arena_t **) malloc(parsec_dtd_no_of_arenas * sizeof(parsec_arena_t *));
    for (i = 0; i < parsec_dtd_no_of_arenas; i++) {
        parsec_dtd_arenas[i] = (parsec_arena_t *) calloc(1, sizeof(parsec_arena_t));
    }
}

/* **************************************************************************** */
/**
 * Fini function of Dynamic Task Discovery Interface.
 *
 * The global mempool of dtd_tp is destroyed here.
 *
 * @ingroup DTD_INTERFACE
 */
void parsec_dtd_fini(void)
{
#if defined(PARSEC_DEBUG_PARANOID)
    assert(parsec_dtd_taskpool_mempool != NULL);
#endif
    int i;
    for( i = 0; i < parsec_dtd_no_of_arenas; i++ ) {
        if (parsec_dtd_arenas[i] != NULL) {
            free(parsec_dtd_arenas[i]);
            parsec_dtd_arenas[i] = NULL;
        }
    }
    free(parsec_dtd_arenas);

    parsec_mempool_destruct( parsec_dtd_tile_mempool );
    free( parsec_dtd_tile_mempool );

    parsec_mempool_destruct( parsec_dtd_taskpool_mempool );
    free( parsec_dtd_taskpool_mempool );

    if(-1 != parsec_dtd_debug_verbose) {
        parsec_output_close(parsec_dtd_debug_output);
        parsec_dtd_debug_output = parsec_debug_output;
    }
}

extern int __parsec_task_progress( parsec_execution_stream_t* es,
                                   parsec_task_t* task,
                                   int distance);

/* **************************************************************************** */
/**
 * Master thread calls this to join worker threads in executing tasks.
 *
 * Master thread, at the end of each window, calls this function to
 * join the worker thread(s) in executing tasks and takes a break
 * from inserting tasks. It(master thread) remains in this function
 * till the total number of pending tasks in the engine reaches a
 * threshold (see parsec_dtd_threshold_size). It goes back to inserting task
 * once the number of pending tasks in the engine reaches the
 * threshold size.
 *
 * @param[in]   context
 *                  The PaRSEC context (pointer to the runtime instance)
 * @param[in]   tp
 *                  PaRSEC dtd taskpool
 *
 * @ingroup     DTD_INTERFACE_INTERNAL
 */
void
parsec_execute_and_come_back( parsec_context_t *context,
                              parsec_taskpool_t *tp,
                              int task_threshold_count )
{
    uint64_t misses_in_a_row;
    parsec_execution_stream_t* es = context->virtual_processes[0]->execution_streams[0];
    parsec_task_t* task;
    int rc, nbiterations = 0, distance;
    struct timespec rqtp;

    rqtp.tv_sec = 0;
    misses_in_a_row = 1;

    /* Checking if the context has been started or not */
    /* The master thread might not have to trigger the barrier if the other
     * threads have been activated by a previous start.
     */
    if( !(PARSEC_CONTEXT_FLAG_CONTEXT_ACTIVE & context->flags) ) {
        (void)parsec_remote_dep_on(context);
        /* Mark the context so that we will skip the initial barrier during the _wait */
        context->flags |= PARSEC_CONTEXT_FLAG_CONTEXT_ACTIVE;
        /* Wake up the other threads */
        parsec_barrier_wait( &(context->barrier) );
    }

    /* we wait for only all the tasks inserted in the taskpool and not for all the communication
     * invoked by those tasks.
     */
    while(tp->nb_tasks > task_threshold_count) {
        if( misses_in_a_row > 1 ) {
            rqtp.tv_nsec = exponential_backoff(misses_in_a_row);
            nanosleep(&rqtp, NULL);
        }
        misses_in_a_row++;  /* assume we fail to extract a task */

        task = current_scheduler->module.select(es, &distance);

        if( task != NULL ) {
            misses_in_a_row = 0;  /* reset the misses counter */

            rc = __parsec_task_progress(es, task, distance);
            (void)rc;
            nbiterations++;
        }
    }
}

/* Function to wait on all pending action of a taskpool */
static int
parsec_dtd_taskpool_wait_on_pending_action(parsec_context_t *parsec,
                                           parsec_taskpool_t  *tp)
{
    (void)parsec;
    struct timespec rqtp;
    rqtp.tv_sec = 0;

    int unit_waited = 0;
    while(tp->nb_pending_actions > 1) {
        unit_waited++;
        if(100 == unit_waited) {
            rqtp.tv_nsec = exponential_backoff(unit_waited);
            nanosleep(&rqtp, NULL);
            unit_waited = 0;
        }
    }
    return 0;
}


/* **************************************************************************** */
/**
 * Function to call when PaRSEC context should wait on a specific taskpool.
 *
 * This function is called to execute a task collection attached to the
 * taskpool by the user. This function will schedule all the initially ready
 * tasks in the engine and return when all the pending tasks are executed.
 * Users should call this function everytime they insert a bunch of tasks.
 * Users can call this function once per taskpool.
 *
 * @param[in]       parsec
 *                      The PaRSEC context
 * @param[in,out]   tp
 *                      PaRSEC dtd taskpool
 *
 * @ingroup         DTD_INTERFACE
 */
int
parsec_dtd_taskpool_wait(parsec_context_t *parsec,
                         parsec_taskpool_t  *tp)
{
    parsec_dtd_taskpool_t *dtd_tp = (parsec_dtd_taskpool_t *)tp;
    parsec_dtd_schedule_tasks(dtd_tp);
    dtd_tp->wait_func(parsec, tp);
    parsec_dtd_taskpool_wait_on_pending_action(parsec, tp);
    return 0;
}

/* This function only waits until all local tasks are done */
static void
parsec_dtd_taskpool_wait_func(parsec_context_t *parsec,
                              parsec_taskpool_t  *tp)
{
    (void)parsec;
    parsec_execute_and_come_back(tp->context, tp, 1);
}

/* **************************************************************************** */
/**
 * This function unpacks the parameters of a task
 *
 * Unpacks all parameters of a task, the variables (in which the actual
 * values will be copied) are passed from the body (function that does what
 * this_task is supposed to compute) of this task and the parameters of each
 * task is copied back on the passed variables
 *
 * @param[in]   this_task
 *                  The task we are trying to unpack the parameters for
 * @param[out]  ...
 *                  The variables where the paramters will be unpacked
 *
 * @ingroup DTD_INTERFACE
 */
void
parsec_dtd_unpack_args(parsec_task_t *this_task, ...)
{
    parsec_dtd_task_t *current_task = (parsec_dtd_task_t *)this_task;
    parsec_dtd_task_param_t *current_param = GET_HEAD_OF_PARAM_LIST(current_task);
    int i = 0;
    void *tmp_val; void **tmp_ref;
    va_list arguments;

    va_start(arguments, this_task);
    while(current_param != NULL) {
        if((current_param->op_type & GET_OP_TYPE) == VALUE) {
            tmp_val = va_arg(arguments, void*);
            memcpy(tmp_val, current_param->pointer_to_tile, current_param->arg_size);
        } else if((current_param->op_type & GET_OP_TYPE) == SCRATCH ||
                  (current_param->op_type & GET_OP_TYPE) == REF) {
            tmp_ref = va_arg(arguments, void**);
            *tmp_ref = current_param->pointer_to_tile;
        } else if((current_param->op_type & GET_OP_TYPE) == INPUT ||
                  (current_param->op_type & GET_OP_TYPE) == INOUT ||
                  (current_param->op_type & GET_OP_TYPE) == OUTPUT) {
            tmp_ref = va_arg(arguments, void**);
            *tmp_ref = PARSEC_DATA_COPY_GET_PTR(this_task->data[i].data_in);
            i++;
        } else {
            parsec_warning("/!\\ Flag is not recognized in parsec_dtd_unpack_args /!\\.\n");
            assert(0);
        }
        current_param = current_param->next;
    }
    va_end(arguments);
}

#if defined(PARSEC_PROF_TRACE)
/* **************************************************************************** */
/**
 * This function returns a unique color
 *
 * This function takes a index and a colorspace and queries unique_color()
 * for a hex color code and prepends "fills:" to that color string.
 *
 * @param[in]   index
 * @param[in]   colorspace
 * @return
 *                  String containing "fill:" followed by color code returned
 *                  by unique_color()
 *
 * @ingroup     DTD_INTERFACE_INTERNAL
 */
static inline char*
fill_color(int index, int colorspace)
{
    char *str, *color;
    str = (char *)calloc(12,sizeof(char));
    color = unique_color(index, colorspace);
    snprintf(str,12,"fill:%s",color+1); /* need to remove the prepended '#' */
    free(color);
    return str;
}

/* **************************************************************************** */
/**
 * This function adds info about a task class into a global dictionary
 * used for profiling.
 *
 * @param[in]   __tp
 *                  The pointer to the DTD taskpool
 * @param[in]   task_class_id
 *                  Id of master structure representing a
 *                  task class
 * @param[in]   name
 *                  Name of the task class
 * @param[in]   flow_count
 *                  Total number of flows of the task class
 *
 * @ingroup     DTD_INTERFACE_INTERNAL
 */
void
parsec_dtd_add_profiling_info( parsec_taskpool_t *tp,
                               int task_class_id,
                               const char* name )
{
    char *str = fill_color(task_class_id, PARSEC_DTD_NB_TASK_CLASSES);
    parsec_profiling_add_dictionary_keyword(name, str,
                                            sizeof(parsec_profile_data_collection_info_t),
                                            PARSEC_PROFILE_DATA_COLLECTION_INFO_CONVERTOR,
                                            (int *) &tp->profiling_array[0 +
                                                                         2 * task_class_id]  /* start key */,
                                            (int *) &tp->profiling_array[1 +
                                                                         2 * task_class_id]  /*  end key */ );
    free(str);
}

void
parsec_dtd_add_profiling_info_generic( parsec_taskpool_t *tp,
                                       const char* name,
                                       int *keyin, int *keyout)
{
    (void)tp;
    char *str = fill_color(*keyin, PARSEC_DTD_NB_TASK_CLASSES);
    parsec_profiling_add_dictionary_keyword(name, str,
                                            sizeof(parsec_profile_data_collection_info_t),
                                            PARSEC_PROFILE_DATA_COLLECTION_INFO_CONVERTOR,
                                            keyin,
                                            keyout);
    free(str);
}

#endif /* defined(PARSEC_PROF_TRACE) */

/* **************************************************************************** */

void
parsec_dtd_track_task( parsec_dtd_taskpool_t *tp,
                       uint64_t               key,
                       void                  *value )
{
    dtd_hash_table_pointer_item_t *item = (dtd_hash_table_pointer_item_t *)parsec_thread_mempool_allocate(tp->hash_table_bucket_mempool->thread_mempools);

    parsec_hash_table_t *hash_table = tp->two_hash_table->task_and_rem_dep_h_table;

    item->ht_item.key   = (parsec_key_t)key;
    item->mempool_owner = tp->hash_table_bucket_mempool->thread_mempools;
    item->value         = (void *)value;

    parsec_hash_table_nolock_insert( hash_table, &item->ht_item );
}

void*
parsec_dtd_find_task( parsec_dtd_taskpool_t *tp,
                      uint64_t               key )
{
    parsec_hash_table_t *hash_table = tp->two_hash_table->task_and_rem_dep_h_table;

    dtd_hash_table_pointer_item_t *item = (dtd_hash_table_pointer_item_t *)parsec_hash_table_nolock_find( hash_table, (parsec_key_t)key );
    return (NULL == item) ? NULL : item->value;
}

void*
parsec_dtd_untrack_task( parsec_dtd_taskpool_t *tp,
                         uint64_t               key )
{
    parsec_hash_table_t *hash_table = tp->two_hash_table->task_and_rem_dep_h_table;
    void *value;

    dtd_hash_table_pointer_item_t *item = (dtd_hash_table_pointer_item_t *)parsec_hash_table_nolock_find( hash_table, (parsec_key_t)key );
    if( NULL == item ) return NULL;

    parsec_hash_table_nolock_remove( hash_table, (parsec_key_t)key );
    value = item->value;
    parsec_thread_mempool_free( tp->hash_table_bucket_mempool->thread_mempools, item );
    return value;
}

void
parsec_dtd_track_remote_dep( parsec_dtd_taskpool_t *tp,
                             uint64_t               key,
                             void                  *value )
{
    parsec_dtd_track_task( tp, key, value );
}

void*
parsec_dtd_find_remote_dep( parsec_dtd_taskpool_t *tp,
                            uint64_t               key )
{
    return parsec_dtd_find_task( tp, key );
}

void*
parsec_dtd_untrack_remote_dep( parsec_dtd_taskpool_t *tp,
                               uint64_t               key )
{
    return parsec_dtd_untrack_task( tp, key );
}

/* **************************************************************************** */
/**
 * This function inserts master structure in hash table
 *
 * @param[in,out]   tp
 *                      Pointer to DTD taskpool, the hash table
 *                      is attached to the taskpool
 * @param[in]       key
 *                      The function-pointer to the body of task-class
 *                      is treated as the key
 * @param[in]       value
 *                      The pointer to the master structure
 *
 * @ingroup         DTD_INTERFACE_INTERNAL
 */
void
parsec_dtd_insert_task_class( parsec_dtd_taskpool_t   *tp,
                              uint64_t  key,
                              parsec_dtd_task_class_t *value )
{
    dtd_hash_table_pointer_item_t *item  =  (dtd_hash_table_pointer_item_t *)parsec_thread_mempool_allocate(tp->hash_table_bucket_mempool->thread_mempools);

    parsec_hash_table_t *hash_table          =  tp->function_h_table;

    item->ht_item.key   = (parsec_key_t)key;
    item->mempool_owner = tp->hash_table_bucket_mempool->thread_mempools;
    item->value         = (void *)value;

    parsec_hash_table_insert ( hash_table, &item->ht_item );
}

/* **************************************************************************** */
/**
 * This function removes master structure from hash table
 *
 * @param[in,out]   tp
 *                      Pointer to DTD taskpool, the hash table
 *                      is attached to the taskpool
 * @param[in]       key
 *                      The function-pointer to the body of task-class
 *                      is treated as the key
 *
 * @ingroup         DTD_INTERFACE_INTERNAL
 */
void
parsec_dtd_remove_task_class( parsec_dtd_taskpool_t  *tp,
                              uint64_t key )
{
    parsec_hash_table_t *hash_table    =  tp->function_h_table;

    parsec_hash_table_remove( hash_table, (parsec_key_t)key );
}

/* **************************************************************************** */
/**
 * This function searches for master-structure in hash table
 *
 * @param[in,out]   tp
 *                      Pointer to DTD taskpool, the hash table
 *                      is attached to the taskpool
 * @param[in]       key
 *                      The function-pointer to the body of task-class
 *                      is treated as the key
 * @return
 *                  The pointer to the master-structure is returned if found,
 *                  NULL otherwise
 *
 * @ingroup         DTD_INTERFACE_INTERNAL
 */
void *
parsec_dtd_find_task_class_internal( parsec_dtd_taskpool_t  *tp,
                                     uint64_t key )
{
    parsec_hash_table_t *hash_table      =  tp->function_h_table;

    return parsec_hash_table_nolock_find ( hash_table, (parsec_key_t)key );
}

/* **************************************************************************** */
/**
 * This function internal API to search for master-structure in hash table
 *
 * @see             parsec_dtd_find_task_class_internal()
 *
 * @param[in,out]   tp
 *                      Pointer to DTD taskpool, the hash table
 *                      is attached to the taskpool
 * @param[in]       key
 *                      The function-pointer to the body of task-class
 *                      is treated as the key
 * @return
 *                  The pointer to the master-structure is returned if found,
 *                  NULL otherwise
 *
 * @ingroup         DTD_INTERFACE_INTERNAL
 */
parsec_dtd_task_class_t *
parsec_dtd_find_task_class( parsec_dtd_taskpool_t  *tp,
                            uint64_t key )
{
    dtd_hash_table_pointer_item_t *item = parsec_dtd_find_task_class_internal( tp, key );
    if( item != NULL ) {
        return (parsec_dtd_task_class_t *)item->value;
    }
    return NULL;
}

/* **************************************************************************** */
/**
 * This function inserts DTD tile into tile hash table
 *
 * The actual key for each tile is formed by OR'ing the last 32 bits of
 * address of the data descriptor (dc) with the 32 bit key. So, the actual
 * key is of 64 bits,
 * first 32 bits: last 32 bits of dc pointer + last 32 bits: 32 bit key.
 * This is done as PaRSEC key for tiles are unique per dc.
 *
 * @param[in,out]   tp
 *                      Pointer to DTD taskpool, the tile hash table
 *                      is attached to the taskpool
 * @param[in]       key
 *                      The key of the tile
 * @param[in]       tile
 *                      Pointer to the tile structure
 * @param[in]       dc
 *                      Pointer to the dc the tile belongs to
 *
 * @ingroup         DTD_ITERFACE_INTERNAL
 */
void
parsec_dtd_tile_insert( uint64_t key,
                        parsec_dtd_tile_t *tile,
                        parsec_data_collection_t    *dc )
{
    parsec_hash_table_t *hash_table = (parsec_hash_table_t *)dc->tile_h_table;

    tile->ht_item.key = (parsec_key_t)key;

    parsec_hash_table_insert( hash_table, &tile->ht_item );
}

/* **************************************************************************** */
/**
 * This function removes DTD tile from tile hash table
 *
 * @param[in,out]   tp
 *                      Pointer to DTD taskpool, the tile hash table
 *                      is attached to this taskpool
 * @param[in]       key
 *                      The key of the tile
 * @param[in]       dc
 *                      Pointer to the dc the tile belongs to
 *
 * @ingroup         DTD_ITERFACE_INTERNAL
 */
void
parsec_dtd_tile_remove( parsec_data_collection_t *dc, uint64_t key )
{
    parsec_hash_table_t *hash_table = (parsec_hash_table_t *)dc->tile_h_table;

    parsec_hash_table_remove( hash_table, (parsec_key_t)key );
}

/* **************************************************************************** */
/**
 * This function searches a DTD tile in the tile hash table
 *
 * @param[in,out]   tp
 *                      Pointer to DTD taskpool, the tile hash table
 *                      is attached to this taskpool
 * @param[in]       key
 *                      The key of the tile
 * @param[in]       dc
 *                      Pointer to the dc the tile belongs to
 *
 * @ingroup         DTD_ITERFACE_INTERNAL
 */
parsec_dtd_tile_t *
parsec_dtd_tile_find( parsec_data_collection_t *dc, uint64_t key )
{
    parsec_hash_table_t *hash_table   = (parsec_hash_table_t *)dc->tile_h_table;
    assert(hash_table != NULL);
    parsec_dtd_tile_t *tile  = (parsec_dtd_tile_t *)parsec_hash_table_nolock_find( hash_table, (parsec_key_t)key );

    return tile;
}

/* **************************************************************************** */
/**
 * This function releases tile from tile hash table and pushes them back in
 * tile mempool
 *
 * Here we at first we try to remove the tile from the tile hash table,
 * if we are successful we push the tile back in the tile mempool.
 * This function is called for each flow of a task, after the task is
 * completed.
 *
 * @param[in,out]   tp
 *                      Pointer to DTD taskpool, the tile hash table
 *                      is attached to this taskpool
 * @param[in]       tile
 *                      Tile to be released
 *
 * @ingroup         DTD_INTERFACE_INTERNAL
 */
void
parsec_dtd_tile_release( parsec_dtd_tile_t *tile )
{
    assert(tile->super.super.obj_reference_count>1);
    if( 2 == parsec_atomic_fetch_dec_int32( &tile->super.super.obj_reference_count ) ) {
        assert(tile->flushed == FLUSHED);
        parsec_thread_mempool_free( parsec_dtd_tile_mempool->thread_mempools, tile );
    }
}

void
parsec_dtd_tile_retain( parsec_dtd_tile_t *tile )
{
    OBJ_RETAIN(tile);
}

/* **************************************************************************** */
/**
 * This function releases the master-structure and pushes them back in mempool
 *
 * @param[in,out]   tp
 *                      Pointer to DTD taskpool, the tile hash table
 *                      is attached to this taskpool
 * @param[in]       key
 *                      The function pointer to the body of the task class
 *
 * @ingroup         DTD_INTERFACE_INTERNAL
 */
void
parsec_dtd_release_task_class( parsec_dtd_taskpool_t *tp,
                               uint64_t key )
{
    dtd_hash_table_pointer_item_t *item = parsec_dtd_find_task_class_internal( tp, key );
#if defined(PARSEC_DEBUG_PARANOID)
    assert (item != NULL);
#endif
    parsec_dtd_remove_task_class( tp, key );
    parsec_thread_mempool_free( tp->hash_table_bucket_mempool->thread_mempools, item );
}

void
parsec_dtd_data_collection_init( parsec_data_collection_t *dc )
{
    int nb;
    dc->tile_h_table = OBJ_NEW(parsec_hash_table_t);
    for(nb = 1; nb < 16 && (1 << nb) < parsec_dtd_tile_hash_table_size; nb++) /* nothing */;
    parsec_hash_table_init( dc->tile_h_table,
                            offsetof(parsec_dtd_tile_t, ht_item),
                            nb,
                            DTD_key_fns,
                            dc->tile_h_table);
    parsec_dc_register_id(dc, parsec_dtd_dc_id++);
}

void
parsec_dtd_data_collection_fini( parsec_data_collection_t *dc )
{
    parsec_hash_table_fini(dc->tile_h_table);
    OBJ_RELEASE(dc->tile_h_table);
    parsec_dc_unregister_id(dc->dc_id);
}

/* **************************************************************************** */
/**
 * Function to recover tiles inserted by insert_task()
 *
 * This function search for a tile if already inserted in the system,
 * and if not returns the freshly created tile.
 *
 * @param[in,out]   parsec taskpool
 *                      Pointer to the DTD taskpool
 * @param[in]       dc
 *                      Data descriptor
 * @param[in]       key
 *                      The data key of the tile in the matrix
 * @return
 *                  The tile representing the data in specified co-ordinate
 *
 * @ingroup         DTD_INTERFACE
 */
parsec_dtd_tile_t*
parsec_dtd_tile_of( parsec_data_collection_t *dc, parsec_data_key_t key )
{
    parsec_dtd_tile_t *tile = parsec_dtd_tile_find ( dc, (uint64_t)key );
    if( NULL == tile ) {
        /* Creating Tile object */
        tile = (parsec_dtd_tile_t *) parsec_thread_mempool_allocate( parsec_dtd_tile_mempool->thread_mempools );
        tile->dc                 = dc;
        tile->arena_index           = -1;
        tile->key                   = (uint64_t) 0x00000000 | key;
        tile->rank                  = dc->rank_of_key(dc, tile->key);
        tile->flushed               = NOT_FLUSHED;
        if( tile->rank == (int)dc->myrank ) {
            tile->data_copy         = (dc->data_of_key(dc, tile->key))->device_copies[0];
#if defined(PARSEC_HAVE_CUDA)
            tile->data_copy->readers = 0;
#endif
        } else {
            tile->data_copy         = NULL;
        }

        SET_LAST_ACCESSOR(tile);
        parsec_dtd_tile_insert( tile->key,
                                tile, dc );
    }
    assert(tile->flushed == NOT_FLUSHED);
#if defined(PARSEC_DEBUG_PARANOID)
    assert(tile->super.super.obj_reference_count > 0);
#endif
    return tile;
}

/* **************************************************************************** */
/**
 * This function acts as the hook to connect the PaRSEC task with
 * the actual task.
 *
 * The function users passed while inserting task in PaRSEC is called
 * in this procedure.
 * Called internally by the scheduler
 *
 * @param[in]   context
 *                  The execution unit
 * @param[in]   this_task
 *                  The DTD task to be executed
 * @return
 *              Returns PARSEC_HOOK_RETURN_DONE if the task was
 *              successfully executed, anything else otherwise
 *
 * @ingroup     DTD_INTERFACE_INTERNAL
 */
static int
hook_of_dtd_task( parsec_execution_stream_t *es,
                  parsec_task_t *this_task )
{
    parsec_dtd_task_t *dtd_task = (parsec_dtd_task_t*)this_task;
    int rc = PARSEC_HOOK_RETURN_DONE;

    assert(parsec_dtd_task_is_local(dtd_task));
    PARSEC_TASK_PROF_TRACE(es->es_profile,
                          this_task->taskpool->profiling_array[2 * this_task->task_class->task_class_id],
                          this_task);

#if !defined(PARSEC_PROF_DRY_BODY)
#if !defined(PARSEC_DRY_RUN)
    rc = ((parsec_dtd_task_class_t *)(dtd_task->super.task_class))->fpointer(es, this_task);
#endif /* !defined(PARSEC_DRY_RUN) */
#endif

    return rc;
}

/* chores and parsec_task_class_t structure initialization */
static const __parsec_chore_t dtd_chore[] = {
    {.type      = PARSEC_DEV_CPU,
     .evaluate  = NULL,
     .hook      = hook_of_dtd_task },
    {.type      = PARSEC_DEV_NONE,
     .evaluate  = NULL,
     .hook      = NULL},             /* End marker */
};

/* for GRAPHER purpose */
static symbol_t symb_dtd_taskid = {
    .name           = "task_id",
    .context_index  = 0,
    .min            = NULL,
    .max            = NULL,
    .cst_inc        = 1,
    .expr_inc       = NULL,
    .flags          = 0x0
};

/**
 * The task class key generator function. For DTD the key is stored in the
 * first assignment value of the task.
 */
static inline parsec_key_t DTD_make_key_identity(const parsec_taskpool_t *tp, const assignment_t *t)
{
    (void)tp;
    return (parsec_key_t)(uintptr_t)t[0].value;
}

void
parsec_dtd_dequeue_taskpool( parsec_taskpool_t *tp )
{
    parsec_dtd_taskpool_t *dtd_tp = (parsec_dtd_taskpool_t *)tp;
    int remaining = parsec_atomic_fetch_dec_int32( &tp->nb_tasks );
    if( 1 == remaining ) {
        (void)parsec_atomic_cas_int32(&tp->nb_tasks, 0, PARSEC_RUNTIME_RESERVED_NB_TASKS);
        dtd_tp->enqueue_flag = 0;
    }

    parsec_dtd_taskpool_release( tp );
    return;  /* we're done in all cases */
}

int
parsec_dtd_update_runtime_task( parsec_taskpool_t *tp, int32_t count )
{
    int32_t remaining;
    remaining = parsec_atomic_fetch_add_int32(&tp->nb_pending_actions, count) + count;
    assert( 0<= remaining );

    if( 0 == remaining && 1 == tp->nb_tasks ) {
        parsec_dtd_dequeue_taskpool( tp );
    }

    return remaining;
}

/* **************************************************************************** */
/**
 * Intializes all the needed members and returns the DTD taskpool
 *
 * For correct profiling the task_class_counter should be correct
 *
 * @param[in]   context
 *                  The PARSEC context
 * @return
 *              The PARSEC DTD taskpool
 *
 * @ingroup     DTD_INTERFACE
 */
parsec_taskpool_t*
parsec_dtd_taskpool_new(void)
{
    if( 0 == parsec_atomic_fetch_inc_int32(&__parsec_dtd_is_initialized) ) {
        parsec_dtd_lazy_init();
    }

    parsec_dtd_taskpool_t *__tp;
    int i;

#if defined(PARSEC_DEBUG_PARANOID)
    assert( parsec_dtd_taskpool_mempool != NULL );
#endif
    __tp = (parsec_dtd_taskpool_t *)parsec_thread_mempool_allocate(parsec_dtd_taskpool_mempool->thread_mempools);

    PARSEC_DEBUG_VERBOSE(parsec_dtd_dump_traversal_info, parsec_dtd_debug_output,
                         "\n\n------ New Taskpool (%p)-----\n\n\n", __tp);

    parsec_dtd_taskpool_retain((parsec_taskpool_t *)__tp);

    __tp->super.context            = NULL;
    __tp->super.on_enqueue         = parsec_dtd_enqueue_taskpool;
    __tp->super.on_enqueue_data    = NULL;
    __tp->super.on_complete        = NULL;
    __tp->super.on_complete_data   = NULL;
    __tp->super.nb_tasks           = PARSEC_RUNTIME_RESERVED_NB_TASKS;
    __tp->super.taskpool_type      = PARSEC_TASKPOOL_TYPE_DTD;  /* Indicating this is a taskpool for dtd tasks */
    __tp->super.nb_pending_actions = 0;  /* For the future tasks that will be inserted */
    __tp->super.nb_task_classes    = 0;
    __tp->super.update_nb_runtime_task = parsec_dtd_update_runtime_task;

    __tp->super.devices_index_mask = 0;
    for (i = 0; i < (int)parsec_nb_devices; i++) {
        parsec_device_t *device = parsec_devices_get(i);
        if( NULL == device ) continue;
        __tp->super.devices_index_mask |= (1 << device->device_index);
    }
    for(i = 0; i < vpmap_get_nb_vp(); i++) {
        __tp->startup_list[i] = NULL;
    }

    /* Keeping track of total tasks to be executed per taskpool for the window */
    for(i = 0; i < PARSEC_DTD_NB_TASK_CLASSES; i++) {
        __tp->flow_set_flag[i]  = 0;
        __tp->super.task_classes_array[i] = NULL;
    }

    __tp->wait_func           = parsec_dtd_taskpool_wait_func;
    __tp->task_id             = 0;
    __tp->task_window_size    = 1;
    __tp->task_threshold_size = parsec_dtd_threshold_size;
    __tp->local_task_inserted = 0;
    __tp->function_counter    = 0;
    __tp->enqueue_flag        = 0;
    parsec_atomic_unlock(&__tp->two_hash_table->atomic_lock);

    (void)parsec_taskpool_reserve_id((parsec_taskpool_t *) __tp);

#if defined(PARSEC_PROF_TRACE) /* TODO: should not be per taskpool */
    if(parsec_dtd_profile_verbose) {
        parsec_dtd_add_profiling_info_generic((parsec_taskpool_t *)__tp, "Insert_task",
                                   &insert_task_trace_keyin, &insert_task_trace_keyout );
        parsec_dtd_add_profiling_info_generic((parsec_taskpool_t *)__tp, "Hash_table_duration",
                                   &hashtable_trace_keyin, &hashtable_trace_keyout);
    }
#endif

    /* The first taskclass of every taskpool is the flush taskclass */
    parsec_dtd_create_task_class(__tp, parsec_dtd_data_flush_sndrcv, "parsec_dtd_data_flush",
                                 0, 0, 1);

    return (parsec_taskpool_t *)__tp;
}

/* **************************************************************************** */
/**
 * Clean up function to clean memory allocated dynamically for the run
 *
 * @param[in,out]   tp
 *                      Pointer to the DTD taskpool
 *
 * @ingroup         DTD_INTERFACE
 */
void
parsec_dtd_taskpool_destruct(parsec_taskpool_t *tp)
{
    parsec_dtd_taskpool_release( tp );
}

void
parsec_dtd_taskpool_retain( parsec_taskpool_t *tp )
{
    OBJ_RETAIN(tp);
}

void
parsec_dtd_taskpool_release( parsec_taskpool_t *tp )
{
    if( 2 == parsec_atomic_fetch_dec_int32( &tp->super.super.obj_reference_count ) ) {
        parsec_dtd_taskpool_t *dtd_tp = (parsec_dtd_taskpool_t *)tp;
        int i;

        for(i = 0; i < PARSEC_DTD_NB_TASK_CLASSES; i++) {
            const parsec_task_class_t *tc = dtd_tp->super.task_classes_array[i];
            parsec_dtd_task_class_t   *dtd_tc = (parsec_dtd_task_class_t *)tc;

            /* Have we reached the end of known functions for this taskpool? */
            if( NULL == tc ) {
                assert(dtd_tp->function_counter == i);
                break;
            }

            uint64_t fkey = (uint64_t)(uintptr_t)dtd_tc->fpointer + tc->nb_flows;
            parsec_dtd_release_task_class( dtd_tp, fkey );

            parsec_dtd_template_release(tc);
        }

        parsec_taskpool_unregister( tp );
        parsec_thread_mempool_free( parsec_dtd_taskpool_mempool->thread_mempools, dtd_tp );
    }
}

/* **************************************************************************** */
/**
 * This is the hook that connects the function to start initial ready
 * tasks with the context. Called internally by PaRSEC.
 *
 * @param[in]   context
 *                  PARSEC context
 * @param[in]   tp
 *                  Pointer to DTD taskpool
 * @param[in]   pready_list
 *                  Lists of ready tasks for each core
 *
 * @ingroup     DTD_INTERFACE_INTERNAL
 */
void
parsec_dtd_startup( parsec_context_t   *context,
                    parsec_taskpool_t  *tp,
                    parsec_task_t     **pready_list )
{
    parsec_dtd_taskpool_t *dtd_tp = (parsec_dtd_taskpool_t *) tp;

    /* Create the PINS DATA pointers if PINS is enabled */
#if defined(PINS_ENABLE)
    dtd_tp->super.context = context;
#endif /* defined(PINS_ENABLE) */

    /* register the taskpool with all available devices */
    for (uint32_t _i = 0; _i < parsec_nb_devices; _i++) {
        parsec_device_t *device = parsec_devices_get(_i);
        if (NULL == device) continue;
        if( !(tp->devices_index_mask & (1 << device->device_index)) ) continue;  /* not supported */
        if (NULL != device->device_taskpool_register)
            if( PARSEC_SUCCESS !=
                device->device_taskpool_register(device, (parsec_taskpool_t *)tp) ) {
                tp->devices_index_mask &= ~(1 << device->device_index);  /* can't use this type */
                continue;
            }
    }
    (void)pready_list;

    parsec_dtd_schedule_tasks( dtd_tp );
}

static inline int
parsec_dtd_not_sent_to_rank(parsec_dtd_task_t *this_task, int flow_index, int dst_rank)
{
    int array_index = dst_rank/(sizeof(int)*8);
    int rank_mask = 1U << (dst_rank % (sizeof(int)*8));

    if( !((FLOW_OF(this_task, flow_index))->rank_sent_to[array_index] & rank_mask) ) {
        (FLOW_OF(this_task, flow_index))->rank_sent_to[array_index] |= rank_mask;
        return 1;
    }
    return 0;
}

/* **************************************************************************** */
/**
 * This function checks the readyness of a task and if ready pushes it
 * in a list of ready task
 *
 * This function will have more functionality in the implementation
 * for distributed memory
 *
 * @param[in]   es
 *                  Execution unit
 * @param[in]   newcontext
 *                  Pointer to DTD task we are trying to activate
 * @param[in]   oldcontext
 *                  Pointer to DTD task activating it's successor(newcontext)
 * @param       dep,data,src_rank,dst_rank,dst_vpid
 *                  Parameters we will use in distributed memory implementation
 * @param[out]  param
 *                  Pointer to list in which we will push if the task is ready
 * @return
 *              Instruction on how to iterate over the successors of oldcontext
 *
 * @ingroup     DTD_INTERFACE_INTERNAL
 */
parsec_ontask_iterate_t
dtd_release_dep_fct( parsec_execution_stream_t *es,
                     const parsec_task_t *newcontext,
                     const parsec_task_t *oldcontext,
                     const dep_t *dep,
                     parsec_dep_data_description_t *data,
                     int src_rank, int dst_rank, int dst_vpid,
                     void *param )
{
    (void)es; (void)data; (void)src_rank; (void)dst_rank; (void)oldcontext;
    parsec_release_dep_fct_arg_t *arg = (parsec_release_dep_fct_arg_t *)param;
    parsec_dtd_task_t *current_task = (parsec_dtd_task_t *)newcontext;
    int32_t not_ready = 1;

#if defined(DISTRIBUTED)
    if( dst_rank != src_rank && src_rank == oldcontext->taskpool->context->my_rank) {
        assert( 0 == (arg->action_mask & PARSEC_ACTION_RECV_INIT_REMOTE_DEPS) );

        if( arg->action_mask & PARSEC_ACTION_SEND_INIT_REMOTE_DEPS ) {
            if( parsec_dtd_not_sent_to_rank((parsec_dtd_task_t *)oldcontext,
                                            dep->belongs_to->flow_index, dst_rank) ) {
                struct remote_dep_output_param_s* output;
                int _array_pos, _array_mask;

#if !defined(PARSEC_DIST_COLLECTIVES)
                assert(src_rank == es->virtual_process->parsec_context->my_rank);
#endif
                _array_pos = dst_rank / (8 * sizeof(uint32_t));
                _array_mask = 1 << (dst_rank % (8 * sizeof(uint32_t)));
                PARSEC_ALLOCATE_REMOTE_DEPS_IF_NULL(arg->remote_deps, oldcontext, MAX_PARAM_COUNT);
                output = &arg->remote_deps->output[dep->dep_datatype_index];
                assert( (-1 == arg->remote_deps->root) || (arg->remote_deps->root == src_rank) );
                arg->remote_deps->root = src_rank;
                /* both sides care about the dep_datatype_index which is the flow_index for DTD */
                arg->remote_deps->outgoing_mask |= (1 << dep->dep_datatype_index);
                if( !(output->rank_bits[_array_pos] & _array_mask) ) {
                    output->rank_bits[_array_pos] |= _array_mask;
                    /* For DTD this means nothing at this point */
                    output->deps_mask |= (1 << dep->dep_index);
                    if( 0 == output->count_bits ) {
                        output->data = *data;
                    } else {
                        assert(output->data.data == data->data);
                    }
                    output->count_bits++;
                    if(newcontext->priority > output->priority) {
                        output->priority = newcontext->priority;
                        if(newcontext->priority > arg->remote_deps->max_priority)
                            arg->remote_deps->max_priority = newcontext->priority;
                    }
                }  /* otherwise the bit is already flipped, the peer is already part of the propagation. */
            }
        }
    }
#else
    (void)src_rank;
    (void)data;
#endif

    if( parsec_dtd_task_is_local(current_task) ) {
        not_ready = parsec_atomic_fetch_dec_int32(&current_task->flow_count) -1;

#if defined(PARSEC_PROF_GRAPHER)
        /* Check to not print stuff redundantly */
        parsec_flow_t *origin_flow = (parsec_flow_t*) calloc(1, sizeof(parsec_flow_t));
        parsec_flow_t *dest_flow = (parsec_flow_t*) calloc(1, sizeof(parsec_flow_t));

        origin_flow->name = "A";
        dest_flow->name = "A";
        dest_flow->flow_flags = FLOW_ACCESS_RW;

        parsec_prof_grapher_dep(oldcontext, newcontext, !not_ready, origin_flow, dest_flow);

        free(origin_flow);
        free(dest_flow);
#else
        (void)dep;
#endif
        if(!not_ready) {
            assert(parsec_dtd_task_is_local(current_task));
#if defined(PARSEC_DEBUG_NOISIER)
            PARSEC_DEBUG_VERBOSE(parsec_dtd_dump_traversal_info, parsec_dtd_debug_output,
                                 "------\ntask Ready: %s \t %" PRIu64 "\nTotal flow: %d  flow_count:"
                                 "%d\n-----\n", current_task->super.task_class->name, current_task->ht_item.key,
                                 current_task->super.task_class->nb_flows, current_task->flow_count);
#endif

            arg->ready_lists[dst_vpid] = (parsec_task_t*)
                parsec_list_item_ring_push_sorted( (parsec_list_item_t*)arg->ready_lists[dst_vpid],
                                                  &current_task->super.super,
                                                  parsec_execution_context_priority_comparator );
            return PARSEC_ITERATE_CONTINUE; /* Returns the status of the task being activated */
        } else {
            return PARSEC_ITERATE_STOP;
        }
    } else {
        return PARSEC_ITERATE_CONTINUE;
    }
}

/* **************************************************************************** */
/**
 * This function iterates over all the successors of a task and
 * activates them and builds a list of the ones that got ready
 * by this activation
 *
 * The actual implementation is done in ordering_correctly_2()
 *
 * @see     ordering_correctly_2()
 *
 * @param   es,this_task,action_mask,ontask,ontask_arg
 *
 * @ingroup DTD_ITERFACE_INTERNAL
 */
static void
parsec_dtd_iterate_successors(parsec_execution_stream_t *es,
                              const parsec_task_t *this_task,
                              uint32_t action_mask,
                              parsec_ontask_function_t *ontask,
                              void *ontask_arg)
{
    parsec_dtd_task_t   *this_dtd_task;

    this_dtd_task = (parsec_dtd_task_t *)this_task;

    (void)this_task; (void)action_mask; (void)ontask; (void)ontask_arg;
    parsec_dtd_ordering_correctly( es, (parsec_task_t *)this_dtd_task,
                                   action_mask, ontask, ontask_arg );
}

/* **************************************************************************** */
/**
 * Release dependencies after a task is done
 *
 * Calls iterate successors function that returns a list of tasks that
 * are ready to go. Those ready tasks are scheduled in here.
 *
 * @param[in]   es
 *                  Execution unit
 * @param[in]   this_task
 *                  Pointer to DTD task we are releasing deps of
 * @param       action_mask,deps
 * @return
 *              0
 *
 * @ingroup     DTD_INTERFACE_INTERNAL
 */
static int
parsec_dtd_release_deps(parsec_execution_stream_t *es,
                        parsec_task_t *this_task,
                        uint32_t action_mask,
                        parsec_remote_deps_t *deps)
{
    (void)deps;
    parsec_release_dep_fct_arg_t arg;
    int __vp_id;

    assert(NULL != es);

    PINS(es, RELEASE_DEPS_BEGIN, this_task);
#if defined(DISTRIBUTED)
    arg.remote_deps = deps;
#endif /* defined(DISTRIBUTED) */

    arg.action_mask  = action_mask;
    arg.output_usage = 0;
    arg.output_entry = NULL;
    arg.ready_lists  = alloca(sizeof(parsec_task_t *) * es->virtual_process->parsec_context->nb_vp);

    for (__vp_id = 0; __vp_id < es->virtual_process->parsec_context->nb_vp; __vp_id++)
        arg.ready_lists[__vp_id] = NULL;

    parsec_dtd_task_t *this_dtd_task = NULL;
    const parsec_task_class_t  *tc = this_task->task_class;
    parsec_dtd_taskpool_t *tp = (parsec_dtd_taskpool_t *)this_task->taskpool;

    if( (action_mask & PARSEC_ACTION_COMPLETE_LOCAL_TASK) ) {
        this_dtd_task = (parsec_dtd_task_t *)this_task;
    } else {
        parsec_dtd_two_hash_table_lock(tp->two_hash_table);
        int flow_index, track_flow = 0;
        for(flow_index = 0; flow_index < tc->nb_flows; flow_index++) {
            if((action_mask & (1 << flow_index))) {
                if(!(track_flow & (1U << flow_index))) {
                    this_dtd_task = parsec_dtd_find_task( tp, (uint64_t)this_task->locals[0].value<<32 | (1U<<flow_index) );
                    assert(this_dtd_task != NULL);

                    if( this_task->data[flow_index].data_out != NULL ) {
                        assert(this_task->data[flow_index].data_out != NULL);
                        this_dtd_task->super.data[flow_index].data_in = this_task->data[flow_index].data_in;
                        this_dtd_task->super.data[flow_index].data_out = this_task->data[flow_index].data_out;
                        parsec_dtd_retain_data_copy(this_task->data[flow_index].data_out);
                    }
                    track_flow |= (1U<<flow_index); /* to make sure we are retaining the data only once */
                }
            }
        }
        parsec_dtd_two_hash_table_unlock(tp->two_hash_table);
    }

    assert(NULL != this_dtd_task);
    tc->iterate_successors(es, (parsec_task_t*)this_dtd_task, action_mask, dtd_release_dep_fct, &arg);

#if defined(DISTRIBUTED)
    /* We perform this only for remote tasks that are being activated
     * from the comm engine. We remove the task from the hash table
     * for each flow a rank is concerned about.
     */
    if( parsec_dtd_task_is_remote(this_dtd_task) && !(action_mask & PARSEC_ACTION_COMPLETE_LOCAL_TASK) ) {
        int flow_index, track_flow = 0;
        parsec_dtd_two_hash_table_lock(tp->two_hash_table);
        for(flow_index = 0; flow_index < tc->nb_flows; flow_index++) {
            if((action_mask & (1 << flow_index))) {
                if(!(track_flow & (1U << flow_index))) {
                    if( NULL != parsec_dtd_untrack_task( tp, ((uint64_t)this_task->locals[0].value<<32 | (1U<<flow_index) )) ) {
                        /* also releasing task */
                        parsec_dtd_remote_task_release( this_dtd_task );
                    }
                    track_flow |= (1U<<flow_index); /* to make sure we are releasing the data only once */
                }
            }
        }
        parsec_dtd_two_hash_table_unlock(tp->two_hash_table);
    }
#else
    (void)deps;
#endif

    /* Scheduling tasks */
    if (action_mask & PARSEC_ACTION_RELEASE_LOCAL_DEPS) {
        parsec_vp_t **vps = es->virtual_process->parsec_context->virtual_processes;
        for (__vp_id = 0; __vp_id < es->virtual_process->parsec_context->nb_vp; __vp_id++) {
            if (NULL == arg.ready_lists[__vp_id]) {
                continue;
            }
            if (__vp_id == es->virtual_process->vp_id) {
                __parsec_schedule(es, arg.ready_lists[__vp_id], 0);
            }else {
                __parsec_schedule(vps[__vp_id]->execution_streams[0], arg.ready_lists[__vp_id], 0);
            }
            arg.ready_lists[__vp_id] = NULL;
        }
    }

    PINS(es, RELEASE_DEPS_END, this_task);
    return 0;
}

/* **************************************************************************** */
/**
 * This function is called internally by PaRSEC once a task is done
 *
 * @param[in]   context
 *                  Execution unit
 * @param[in]   this_task
 *                  Pointer to DTD task we just completed
 * @return
 *              0
 *
 * @ingroup     DTD_INTERFACE_INTERNAL
 */
static parsec_hook_return_t
complete_hook_of_dtd( parsec_execution_stream_t *es,
                      parsec_task_t *this_task )
{
    /* Assuming we only call this function for local tasks */
    parsec_dtd_task_t *this_dtd_task = (parsec_dtd_task_t *) this_task;
    int action_mask = 0;
#if defined(PARSEC_DEBUG_NOISIER)
    static int32_t atomic_internal_counter = 0;
#endif  /* defined(PARSEC_DEBUG_NOISIER) */

    PARSEC_DEBUG_VERBOSE(parsec_dtd_dump_traversal_info, parsec_dtd_debug_output,
                         "------------------------------------------------\n"
                         "execution done of task: %s \t %" PRIu64 "\n"
                         "task done %u rank --> %d\n",
                         this_task->task_class->name,
                         this_dtd_task->ht_item.key,
                         parsec_atomic_fetch_inc_int32(&atomic_internal_counter) + 1,
                         this_task->taskpool->context->my_rank);

#if defined(PARSEC_PROF_GRAPHER)
    parsec_prof_grapher_task(this_task, es->th_id, es->virtual_process->vp_id,
                             this_task->task_class->key_functions->key_hash(this_task->task_class->make_key( this_task->taskpool, this_task->locals ),
                                                                            64, NULL));
#endif /* defined(PARSEC_PROF_GRAPHER) */

    PARSEC_TASK_PROF_TRACE(es->es_profile,
                           this_task->taskpool->profiling_array[2 * this_task->task_class->task_class_id + 1],
                           this_task);

    /* constructing action_mask for all flows of local task */
    int current_dep;
    for( current_dep = 0; current_dep < this_dtd_task->super.task_class->nb_flows; current_dep++ ) {
        action_mask |= (1U << current_dep);
    }

    this_task->task_class->release_deps(es, this_task, action_mask          |
                                        PARSEC_ACTION_RELEASE_LOCAL_DEPS    |
                                        PARSEC_ACTION_SEND_REMOTE_DEPS      |
                                        PARSEC_ACTION_SEND_INIT_REMOTE_DEPS |
                                        PARSEC_ACTION_RELEASE_REMOTE_DEPS   |
                                        PARSEC_ACTION_COMPLETE_LOCAL_TASK,
                                        NULL);

    return 0;
}

parsec_hook_return_t
parsec_dtd_release_local_task( parsec_dtd_task_t *this_task )
{
    parsec_object_t *object = (parsec_object_t *)this_task;
    assert(this_task->super.super.super.obj_reference_count > 1);
    if( 2 == parsec_atomic_fetch_dec_int32( &object->obj_reference_count ) ) {
        int current_flow;
        for( current_flow = 0; current_flow < this_task->super.task_class->nb_flows; current_flow++ ) {
            parsec_dtd_tile_t *tile = (FLOW_OF(this_task, current_flow))->tile;
            if( tile == NULL ) continue;
            assert( NULL != this_task->super.data[current_flow].data_in );
            if( !((FLOW_OF(this_task, current_flow))->op_type & DONT_TRACK) ) {
                if( !((FLOW_OF(this_task, current_flow))->flags & DATA_RELEASED) ) {
                    (FLOW_OF(this_task, current_flow))->flags |= DATA_RELEASED;
                    parsec_dtd_release_data_copy(this_task->super.data[current_flow].data_in);
                }
            }
            if(PARSEC_DTD_FLUSH_TC_ID == this_task->super.task_class->task_class_id) {
                assert( current_flow == 0 );
                parsec_dtd_tile_release( tile );
            }
        }
        assert(this_task->super.super.super.obj_reference_count == 1);
        parsec_taskpool_t *tp = this_task->super.taskpool;

        parsec_thread_mempool_free( this_task->mempool_owner, this_task );
        parsec_taskpool_update_runtime_nbtask( tp, -1 );
    }
    return PARSEC_HOOK_RETURN_DONE;
}

/* Function to push back tasks in their mempool once the execution are done */
parsec_hook_return_t
parsec_release_dtd_task_to_mempool(parsec_execution_stream_t *es,
                                  parsec_task_t *this_task)
{
    (void)es;
    (void)parsec_atomic_fetch_dec_int32( &this_task->taskpool->nb_tasks );
    return parsec_dtd_release_local_task( (parsec_dtd_task_t *)this_task );
}

void
parsec_dtd_remote_task_retain( parsec_dtd_task_t *this_task )
{
    parsec_object_t *object = (parsec_object_t *)this_task;
    (void)parsec_atomic_fetch_inc_int32(&object->obj_reference_count);
}

void
parsec_dtd_remote_task_release( parsec_dtd_task_t *this_task )
{
    parsec_object_t *object = (parsec_object_t *)this_task;
    assert(object->obj_reference_count > 1);
    if( 2 == parsec_atomic_fetch_dec_int32( &object->obj_reference_count ) ){
        int current_flow;
        for( current_flow = 0; current_flow < this_task->super.task_class->nb_flows; current_flow++ ) {
            if( !((FLOW_OF(this_task, current_flow))->op_type & DONT_TRACK) ) {
                if( NULL != this_task->super.data[current_flow].data_out ) {
                    parsec_dtd_release_data_copy(this_task->super.data[current_flow].data_out);
                }
            }

            parsec_dtd_tile_t *tile = (FLOW_OF(this_task, current_flow))->tile;
            if( tile == NULL ) continue;
            if(PARSEC_DTD_FLUSH_TC_ID == this_task->super.task_class->task_class_id) {
                assert( current_flow == 0 );
                parsec_dtd_tile_release( tile );
            }
        }
        assert(this_task->super.super.super.obj_reference_count == 1);
        parsec_taskpool_t *tp = this_task->super.taskpool;
        parsec_thread_mempool_free( this_task->mempool_owner, this_task );
        parsec_taskpool_update_runtime_nbtask( tp, -1 );
    }
    assert(object->obj_reference_count >= 1);
}

/* Prepare_input function */
int
data_lookup_of_dtd_task( parsec_execution_stream_t *es,
                         parsec_task_t *this_task )
{
    (void)es;

    int current_dep, op_type_on_current_flow;
    parsec_dtd_task_t *current_task = (parsec_dtd_task_t *)this_task;

    for( current_dep = 0; current_dep < current_task->super.task_class->nb_flows; current_dep++ ) {
        op_type_on_current_flow = ((FLOW_OF(current_task, current_dep))->op_type & GET_OP_TYPE);

        if( NULL == current_task->super.data[current_dep].data_in ) continue;

        if( INOUT == op_type_on_current_flow ||
            OUTPUT == op_type_on_current_flow ) {
            if( current_task->super.data[current_dep].data_in->readers > 0 ) {
                return PARSEC_HOOK_RETURN_AGAIN;
            }
        }
    }

    return PARSEC_HOOK_RETURN_DONE;
}

/* Prepare_output function */
int
output_data_of_dtd_task( parsec_execution_stream_t *es,
                         parsec_task_t *this_task )
{
    (void)es;

    int current_dep;
    parsec_dtd_task_t *current_task = (parsec_dtd_task_t *)this_task;

    for( current_dep = 0; current_dep < current_task->super.task_class->nb_flows; current_dep++ ) {
        /* The following check makes sure the behavior is correct when NULL is provided as input to a flow.
           Dependency tracking and building is completely ignored in flow = NULL case, and no expectation should exist.
        */
        if( NULL == current_task->super.data[current_dep].data_in &&
            NULL != current_task->super.data[current_dep].data_out ) {
            parsec_fatal( "Please make sure you are not assigning data in data_out when data_in is provided as NULL.\n"
                          "No dependency tracking is performed for a flow when NULL is passed as as INPUT for that flow.\n" );
        }

        if( NULL != current_task->super.data[current_dep].data_in ) {
            current_task->super.data[current_dep].data_out = current_task->super.data[current_dep].data_in;
        }
    }

    return PARSEC_HOOK_RETURN_DONE;
}

static int datatype_lookup_of_dtd_task(parsec_execution_stream_t *es,
                                       const parsec_task_t *this_task,
                                       uint32_t *flow_mask, parsec_dep_data_description_t *data)
{
    (void)es;
    data->count = 1;
    data->displ = 0;

    int i;
    for( i = 0; i < this_task->task_class->nb_flows; i++) {
        if((*flow_mask) & (1U<<i)) {
            data->arena  = parsec_dtd_arenas[(FLOW_OF(((parsec_dtd_task_t *)this_task), i))->arena_index];
            data->layout = data->arena->opaque_dtt;
            (*flow_mask) &= ~(1U<<i);
            return PARSEC_HOOK_RETURN_NEXT;
        }
    }

    data->arena  = NULL;
    data->data   = NULL;
    data->layout = PARSEC_DATATYPE_NULL;
    data->count  = 0;
    data->displ  = 0;
    (*flow_mask) = 0;		/* nothing left */

    return PARSEC_HOOK_RETURN_DONE;
}

/* This function creates relationship between two task function classes.
 * Arguments:   - parsec taskpool (parsec_taskpool_t *)
                - parent master structure (parsec_task_class_t *)
                - child master structure (parsec_task_class_t *)
                - flow index of task that belongs to the class of "parent master structure" (int)
                - flow index of task that belongs to the class of "child master structure" (int)
                - the type of data (the structure of the data like square,
                  triangular and etc) this dependency is about (int)
 * Returns:     - void
 */
void
set_dependencies_for_function(parsec_taskpool_t* tp,
                              parsec_task_class_t *parent_tc,
                              parsec_task_class_t *desc_tc,
                              uint8_t parent_flow_index,
                              uint8_t desc_flow_index)
{
    (void) tp;
    /* In this function we do not create deps between flow's any more. We just
     * intialize the flow structures of the task classes accordingly.
     */

    if(NULL == desc_tc && NULL != parent_tc) { /* Data is not going to any other task */
        parsec_flow_t *parent_out = (parsec_flow_t*)(parent_tc->out[parent_flow_index]);
        parent_out->flow_datatype_mask |= (1U<<parent_flow_index);
    } else if (NULL == parent_tc && NULL != desc_tc) {
        parsec_flow_t *desc_in = (parsec_flow_t*)(desc_tc->in[desc_flow_index]);
        desc_in->flow_datatype_mask |= (1U<<desc_flow_index);
    } else {
        /* In this case it means we have both parent and child task_class */
        parsec_flow_t *parent_out = (parsec_flow_t*)(parent_tc->out[parent_flow_index]);
        parent_out->flow_datatype_mask |= (1U<<parent_flow_index);

        parsec_flow_t *desc_in = (parsec_flow_t*)(desc_tc->in[desc_flow_index]);
        desc_in->flow_datatype_mask |= (1U<<desc_flow_index);
    }
}

/* **************************************************************************** */
/**
 * This function creates and initializes members of master-structure
 * representing each task-class
 *
 * Most importantly mempool from which we will create DTD task of a
 * task class is created here. The size of each task is calculated
 * from the parameters passed to this function. A master-structure
 * is like a template of a task class. It store the common attributes
 * of all the tasks, belonging to a task class, will have.
 *
 * @param[in,out]   __tp
 *                      The DTD taskpool
 * @paramp[in]      fpointer
 *                      Function pointer that uniquely identifies a
 *                      task class, this is the pointer to the body
 *                      of the task
 * @param[in]       name
 *                      The name of the task class the master-structure
 *                      will represent
 * @param[in]       count_of_params
 *                      Total count of parameters each task of this task
 *                      class will have and work on
 * @param[in]       size_of_param
 *                      Total size of all params in bytes
 * @param[in]       flow_count
 *                      Total flow each task of this task class has
 * @return
 *                  The master-structure
 *
 * @ingroup         DTD_INTERFACE_INTERNAL
 */
parsec_task_class_t*
parsec_dtd_create_task_class( parsec_dtd_taskpool_t *__tp, parsec_dtd_funcptr_t* fpointer,
                              const char* name, int count_of_params, long unsigned int size_of_param,
                              int flow_count )
{
    parsec_dtd_task_class_t *dtd_tc = (parsec_dtd_task_class_t *) calloc(1, sizeof(parsec_dtd_task_class_t));
    parsec_task_class_t *tc = (parsec_task_class_t *)dtd_tc;

    dtd_tc->dep_datatype_index = 0;
    dtd_tc->dep_in_index       = 0;
    dtd_tc->dep_out_index      = 0;
    dtd_tc->count_of_params    = count_of_params;
    dtd_tc->size_of_param      = size_of_param;
    dtd_tc->fpointer           = fpointer;
    dtd_tc->ref_count          = 1;

    /* Allocating mempool according to the size and param count */
    int total_size =  sizeof(parsec_dtd_task_t) +
                     (flow_count * sizeof(parsec_dtd_parent_info_t)) +
                     (flow_count * sizeof(parsec_dtd_descendant_info_t)) +
                     (flow_count * sizeof(parsec_dtd_flow_info_t)) +
                     (count_of_params * sizeof(parsec_dtd_task_param_t)) +
                      size_of_param;

    parsec_mempool_construct( &dtd_tc->context_mempool,
                              OBJ_CLASS(parsec_dtd_task_t), total_size,
                              offsetof(parsec_dtd_task_t, mempool_owner),
                              1/* no. of threads*/ );

    int total_size_remote_task = sizeof(parsec_dtd_task_t) +
                     (flow_count * sizeof(parsec_dtd_parent_info_t)) +
                     (flow_count * sizeof(parsec_dtd_descendant_info_t)) +
                     (flow_count * sizeof(parsec_dtd_min_flow_info_t));

    parsec_mempool_construct( &dtd_tc->remote_task_mempool,
                              OBJ_CLASS(parsec_dtd_task_t), total_size_remote_task,
                              offsetof(parsec_dtd_task_t, mempool_owner),
                              1/* no. of threads*/ );

    /*
     To bypass const in function structure.
     Getting address of the const members in local mutable pointers.
     */
    const char **name_not_const = (const char **)&(tc->name);
    symbol_t **params     = (symbol_t **) &tc->params;
    symbol_t **locals     = (symbol_t **) &tc->locals;
    expr_t **priority     = (expr_t **)&tc->priority;
    __parsec_chore_t **incarnations = (__parsec_chore_t **)&(tc->incarnations);

    *name_not_const           = name;
    tc->task_class_id         = __tp->function_counter++;
    tc->nb_flows              = flow_count;
    /* set to one so that prof_grpaher prints the task id properly */
    tc->nb_parameters         = 1;
    tc->nb_locals             = 1;
    params[0]                 = &symb_dtd_taskid;
    locals[0]                 = &symb_dtd_taskid;
    tc->data_affinity         = NULL;
    tc->initial_data          = NULL;
    tc->final_data            = (parsec_data_ref_fn_t *) NULL;
    *priority                 = NULL;
    tc->flags                 = 0x0 | PARSEC_HAS_IN_IN_DEPENDENCIES | PARSEC_USE_DEPS_MASK;
    tc->dependencies_goal     = 0;
    tc->make_key              = DTD_make_key_identity;
    tc->key_functions         = &DTD_key_fns;
    tc->fini                  = NULL;
    *incarnations             = (__parsec_chore_t *)dtd_chore;
    tc->find_deps             = NULL;
    tc->iterate_successors    = parsec_dtd_iterate_successors;
    tc->iterate_predecessors  = NULL;
    tc->release_deps          = parsec_dtd_release_deps;
    tc->prepare_input         = data_lookup_of_dtd_task;
    tc->prepare_output        = output_data_of_dtd_task;
    tc->get_datatype          = (parsec_datatype_lookup_t *)datatype_lookup_of_dtd_task,
    tc->complete_execution    = complete_hook_of_dtd;
    tc->release_task          = parsec_release_dtd_task_to_mempool;

    /* Inserting Function structure in the hash table to keep track for each class of task */
    uint64_t fkey = (uint64_t)(uintptr_t)fpointer + tc->nb_flows;
    parsec_dtd_insert_task_class( __tp, fkey, dtd_tc );
    assert( NULL == __tp->super.task_classes_array[tc->task_class_id] );
    __tp->super.task_classes_array[tc->task_class_id]     = (parsec_task_class_t *)tc;
    __tp->super.task_classes_array[tc->task_class_id + 1] = NULL;

    __tp->super.nb_task_classes++;

    return tc;
}

void
parsec_dtd_destroy_template( const parsec_task_class_t *tc )
{
    parsec_dtd_task_class_t *dtd_tc = (parsec_dtd_task_class_t *)tc;
    int j, k;

    /* As we fill the flows and then deps in sequential order, we can bail out at the first NULL */
    for (j = 0; j < tc->nb_flows; j++) {
        if( NULL == tc->in[j] ) break;
        for(k = 0; k < MAX_DEP_IN_COUNT; k++) {
            if ( NULL == tc->in[j]->dep_in[k] ) break;
            free((void*)tc->in[j]->dep_in[k]);
        }
        for(k = 0; k < MAX_DEP_OUT_COUNT; k++) {
            if ( NULL == tc->in[j]->dep_out[k] ) break;
            free((void*)tc->in[j]->dep_out[k]);
        }
        free((void*)tc->in[j]);
    }
    parsec_mempool_destruct(&dtd_tc->context_mempool);
    parsec_mempool_destruct(&dtd_tc->remote_task_mempool);
    free((void*)tc);
}

void
parsec_dtd_template_retain( const parsec_task_class_t *tc )
{
    ((parsec_dtd_task_class_t *)tc)->ref_count++;
}

void
parsec_dtd_template_release( const parsec_task_class_t *tc )
{
    ((parsec_dtd_task_class_t *)tc)->ref_count -= 1;
    if( 0 == ((parsec_dtd_task_class_t *)tc)->ref_count ) {
        parsec_dtd_destroy_template(tc);
    }
}

/* **************************************************************************** */
/**
 * This function sets the flows in master-structure as we discover them
 *
 * @param[in,out]   __tp
 *                      DTD taskpool
 * @param[in]       this_task
 *                      Task to point to correct master-structure
 * @param[in]       tile_op_type
 *                      The operation type of the task on the flow
 *                      we are setting here
 * @param[in]       flow_index
 *                      The index of the flow we are setting
 *
 * @ingroup         DTD_INTERFACE_INTERNAL
 */
void
parsec_dtd_set_flow_in_function(parsec_dtd_taskpool_t *dtd_tp,
                                parsec_dtd_task_t *this_task, int tile_op_type,
                                int flow_index)
{
    (void)dtd_tp;
    parsec_flow_t* flow  = (parsec_flow_t *) calloc(1, sizeof(parsec_flow_t));
    flow->name          = "Random";
    flow->sym_type      = 0;
    flow->flow_index    = flow_index;
    flow->flow_datatype_mask = 0;

    int i;
    for (i=0; i<MAX_DEP_IN_COUNT; i++) {
        flow->dep_in[i] = NULL;
    }
    for (i=0; i<MAX_DEP_OUT_COUNT; i++) {
        flow->dep_out[i] = NULL;
    }

    if ((tile_op_type & GET_OP_TYPE) == INPUT) {
        flow->flow_flags = FLOW_ACCESS_READ;
    } else if ((tile_op_type & GET_OP_TYPE) == OUTPUT || (tile_op_type & GET_OP_TYPE) == ATOMIC_WRITE) {
        flow->flow_flags = FLOW_ACCESS_WRITE;
    } else if ((tile_op_type & GET_OP_TYPE) == INOUT) {
        flow->flow_flags = FLOW_ACCESS_RW;
    }

    parsec_flow_t **in = (parsec_flow_t **)&(this_task->super.task_class->in[flow_index]);
    *in = flow;
    parsec_flow_t **out = (parsec_flow_t **)&(this_task->super.task_class->out[flow_index]);
    *out = flow;
}

/* **************************************************************************** */
/**
 * This function sets the parent of a task
 *
 * This function is called by the descendant and the descendant here
 * puts itself as the descendant of the parent.
 *
 * @param[out]  parent_task
 *                  Task we are setting descendant for
 * @param[in]   parent_flow_index
 *                  Flow index of parent for which the
 *                  descendant is being set
 * @param[in]   desc_task
 *                  The descendant
 * @param[in]   desc_flow_index
 *                  Flow index of descendant
 * @param[in]   parent_op_type
 *                  Operation type of parent task on its flow
 * @param[in]   desc_op_type
 *                  Operation type of descendant task on its flow
 *
 * @ingroup     DTD_INTERFACE_INTERNAL
 */
void
parsec_dtd_set_parent(parsec_dtd_task_t *parent_task, uint8_t parent_flow_index,
                      parsec_dtd_task_t *desc_task, uint8_t desc_flow_index,
                      int parent_op_type, int desc_op_type)
{
    parsec_dtd_parent_info_t *parent = PARENT_OF(desc_task, desc_flow_index);
    (void)desc_op_type;
    parent->task       = parent_task;
    parent->op_type    = parent_op_type;
    parent->flow_index = parent_flow_index;
}

/* **************************************************************************** */
/**
 * This function sets the descendant of a task
 *
 * This function is called by the descendant and the descendant here
 * puts itself as the descendant of the parent.
 *
 * @param[out]  parent_task
 *                  Task we are setting descendant for
 * @param[in]   parent_flow_index
 *                  Flow index of parent for which the
 *                  descendant is being set
 * @param[in]   desc_task
 *                  The descendant
 * @param[in]   desc_flow_index
 *                  Flow index of descendant
 * @param[in]   parent_op_type
 *                  Operation type of parent task on its flow
 * @param[in]   desc_op_type
 *                  Operation type of descendant task on its flow
 *
 * @ingroup     DTD_INTERFACE_INTERNAL
 */
void
parsec_dtd_set_descendant(parsec_dtd_task_t *parent_task, uint8_t parent_flow_index,
                          parsec_dtd_task_t *desc_task, uint8_t desc_flow_index,
                          int parent_op_type, int desc_op_type, int last_user_alive)
{
    parsec_dtd_taskpool_t *tp  = (parsec_dtd_taskpool_t *)parent_task->super.taskpool;
    parsec_dtd_task_t *real_parent_task = (PARENT_OF(desc_task, desc_flow_index))->task;
    int real_parent_flow_index          = (PARENT_OF(desc_task, desc_flow_index))->flow_index;

    (void)parent_op_type;
    parsec_dtd_descendant_info_t *desc = DESC_OF(parent_task, parent_flow_index);
    desc->flow_index = desc_flow_index;
    desc->op_type    = desc_op_type;
    parsec_mfence();
    desc->task       = desc_task;

#if defined(DISTRIBUTED)
    /* only do if parent is remote and desc is local and parent has not been activated by remote node */
    if( parsec_dtd_task_is_remote(real_parent_task) && parsec_dtd_task_is_local(desc_task) && last_user_alive == TASK_IS_ALIVE) {
        parsec_dtd_flow_info_t *flow = FLOW_OF(real_parent_task, real_parent_flow_index);

        /* Marking we need the parent task */
        if( !(flow->flags & TASK_INSERTED) ) {
            parsec_dtd_remote_task_retain( real_parent_task );
        }

        /* Locking the two hash table */
        parsec_dtd_two_hash_table_lock(tp->two_hash_table);

        uint64_t key = (((uint64_t)real_parent_task->ht_item.key)<<32) | (1U<<real_parent_flow_index);
        parsec_remote_deps_t *dep = parsec_dtd_find_remote_dep( tp, key );
        if( NULL == dep ) {
            if( !(flow->flags & TASK_INSERTED) ) {
                flow->flags |= TASK_INSERTED;
                parsec_dtd_track_task( tp, key, real_parent_task );
            }
        } else {
            if( !(flow->flags & TASK_INSERTED) ) {
                assert(dep->from == real_parent_task->rank);
                flow->flags |= TASK_INSERTED;
                parsec_dtd_untrack_remote_dep( tp, key );
#if defined(PARSEC_PROF_TRACE)
                if(parsec_dtd_profile_verbose)
                    parsec_profiling_ts_trace(hashtable_trace_keyin, 0, tp->super.taskpool_id, NULL);
#endif
                parsec_dtd_track_task( tp, key, real_parent_task );
                remote_dep_dequeue_delayed_dep_release(dep);
            }
        }

        /* Unlocking the two hash table */
        parsec_dtd_two_hash_table_unlock(tp->two_hash_table);
    }
#endif
}

/* **************************************************************************** */
/**
 * Function to push ready task in PaRSEC's scheduler
 *
 * @param[in,out]   __tp
 *                      DTD taskpool
 *
 * @ingroup         DTD_INTERFACE_INTERNAL
 */
void
parsec_dtd_schedule_tasks( parsec_dtd_taskpool_t *__tp )
{
    parsec_task_t **startup_list = __tp->startup_list;
    parsec_list_t temp;

    OBJ_CONSTRUCT( &temp, parsec_list_t );
    for(int p = 0; p < vpmap_get_nb_vp(); p++) {
        if( NULL == startup_list[p] ) continue;

        /* Order the tasks by priority */
        parsec_list_chain_sorted(&temp, (parsec_list_item_t*)startup_list[p],
                                parsec_execution_context_priority_comparator);
        startup_list[p] = (parsec_task_t*)parsec_list_nolock_unchain(&temp);
        /* We should add these tasks on the system queue when there is one */
        __parsec_schedule( __tp->super.context->virtual_processes[p]->execution_streams[0],
                          startup_list[p], 0 );
        startup_list[p] = NULL;
    }
    OBJ_DESTRUCT(&temp);
}

/* **************************************************************************** */
/**
 * Create and initialize a dtd task
 *
 */
parsec_dtd_task_t *
parsec_dtd_create_and_initialize_task( parsec_dtd_taskpool_t *dtd_tp,
                                       parsec_task_class_t *tc,
                                       int rank )
{
    int i;
    parsec_dtd_task_t *this_task;
    assert( NULL != dtd_tp );
    assert( NULL != tc );

    parsec_mempool_t *dtd_task_mempool;
    /* Creating Task object */
    if(dtd_tp->super.context->my_rank == rank) {
        dtd_task_mempool = &((parsec_dtd_task_class_t*)tc)->context_mempool;
    } else {
        dtd_task_mempool = &((parsec_dtd_task_class_t*)tc)->remote_task_mempool;
    }
    this_task = (parsec_dtd_task_t *)parsec_thread_mempool_allocate(dtd_task_mempool->thread_mempools);

    assert(this_task->super.super.super.obj_reference_count == 1);

    this_task->orig_task = NULL;
    this_task->super.taskpool   = (parsec_taskpool_t*)dtd_tp;
    this_task->ht_item.key      = (parsec_key_t)(uintptr_t)(dtd_tp->task_id++);
    /* this is needed for grapher to work properly */
    this_task->super.locals[0].value = (int)(uintptr_t)this_task->ht_item.key;
    assert( (uintptr_t)this_task->super.locals[0].value == (uintptr_t)this_task->ht_item.key );
    this_task->super.task_class      = tc;
    /**
     * +1 to make sure the task cannot be completed by the potential predecessors,
     * before we are completely done with it here. As we have an atomic operation
     * in all cases, increasing the expected flows by one will have no impact on
     * the performance.
     * */
    this_task->flow_count     = this_task->super.task_class->nb_flows + 1;
    this_task->rank           = rank;
    this_task->super.priority = 0;
    this_task->super.chore_id = 0;
    this_task->super.status   = PARSEC_TASK_STATUS_NONE;

    int j;
    parsec_dtd_flow_info_t *flow;
    parsec_dtd_descendant_info_t *desc;
    for( i = 0; i < tc->nb_flows; i++ ) {
        flow = FLOW_OF(this_task, i);
        if( parsec_dtd_task_is_local(this_task) ) {
            for( j = 0; j < (int)( dtd_tp->super.context->nb_nodes / (sizeof(int)*8) ) + 1; j++) {
                flow->rank_sent_to[j] = 0;
            }
        }
        flow->op_type = 0;
        flow->tile    = NULL;

        desc = DESC_OF(this_task, i);
        desc->op_type    = 0;
        desc->flow_index = -1;
        desc->task       = NULL;
    }

    return this_task;
}

/* **************************************************************************** */
/**
 * Function to set parameters of a dtd task
 *
 */
void
parsec_dtd_set_params_of_task( parsec_dtd_task_t *this_task, parsec_dtd_tile_t *tile,
                               int tile_op_type, int *flow_index, void **current_val,
                               parsec_dtd_task_param_t *current_param, int arg_size )
{
    if( (tile_op_type & GET_OP_TYPE) == INPUT  ||
        (tile_op_type & GET_OP_TYPE) == OUTPUT ||
        (tile_op_type & GET_OP_TYPE) == INOUT  ||
        (tile_op_type & GET_OP_TYPE) == ATOMIC_WRITE)
    {
        this_task->super.data[*flow_index].data_in   = NULL;
        this_task->super.data[*flow_index].data_out  = NULL;
        this_task->super.data[*flow_index].data_repo = NULL;

        parsec_dtd_flow_info_t *flow = FLOW_OF(this_task, *flow_index);
        /* Saving tile pointer for each flow in a task */
        flow->tile    = tile;
        flow->flags   = 0;
        flow->arena_index = -1;
        flow->op_type = tile_op_type;

        *flow_index += 1;
    } else if ((tile_op_type & GET_OP_TYPE) == REF) {
        assert(NULL != tile);
        current_param->pointer_to_tile = (void *)tile;
       *current_val = ((char*)*current_val) + arg_size;
    } else if ((tile_op_type & GET_OP_TYPE) == SCRATCH) {
        assert(parsec_dtd_task_is_local(this_task));
        if(NULL == tile) {
            current_param->pointer_to_tile = *current_val;
        } else {
            current_param->pointer_to_tile = (void *)tile;
        }
       *current_val = ((char*)*current_val) + arg_size;
    } else if((tile_op_type & GET_OP_TYPE) == VALUE) {
        /* Once we get a value, we check the size,
         * and if the size is between 4 to 8 we treat
         * them as constant
         */
        assert(parsec_dtd_task_is_local(this_task));
        memcpy(*current_val, (void *)tile, arg_size);
        current_param->pointer_to_tile = *current_val;
       *current_val = ((char*)*current_val) + arg_size;
    }
}

/* **************************************************************************** */
/**
 * Body of fake task we insert before every INPUT task that reads
 * from memory or remote tasks that reads from local memory
 *
 * @param   context, this_task
 *
 * @ingroup DTD_INTERFACE_INTERNAL
 */
int
fake_first_out_body( parsec_execution_stream_t *es, parsec_task_t *this_task)
{
    (void)es; (void)this_task;
    return PARSEC_HOOK_RETURN_DONE;
}

int
parsec_dtd_schedule_task_if_ready(int satisfied_flow, parsec_dtd_task_t *this_task,
                                  parsec_dtd_taskpool_t *dtd_tp, int *vpid)
{
    /* Building list of initial ready task */
    if ( satisfied_flow == parsec_atomic_fetch_sub_int32(&this_task->flow_count, satisfied_flow) ) {
        PARSEC_DEBUG_VERBOSE(parsec_dtd_dump_traversal_info, parsec_dtd_debug_output,
                             "------\ntask Ready: %s \t %lld\nTotal flow: %d  flow_count:"
                             "%d\n-----\n", this_task->super.task_class->name, this_task->ht_item.key,
                             this_task->super.task_class->nb_flows, this_task->flow_count);

        PARSEC_LIST_ITEM_SINGLETON(this_task);
        __parsec_schedule( dtd_tp->super.context->virtual_processes[*vpid]->execution_streams[0],
                           (parsec_task_t *)this_task, 0 );
        *vpid = (*vpid+1)%dtd_tp->super.context->nb_vp;
        return 1; /* Indicating local task was ready */
    }
    return 0;
}

int
parsec_dtd_block_if_threshold_reached(parsec_dtd_taskpool_t *dtd_tp, int task_threshold)
{
    if( (dtd_tp->local_task_inserted % dtd_tp->task_window_size) == 0 ) {
        if( dtd_tp->task_window_size < parsec_dtd_window_size ) {
            dtd_tp->task_window_size *= 2;
        } else {
            parsec_execute_and_come_back(dtd_tp->super.context, &dtd_tp->super,
                                         task_threshold);
            return 1; /* Indicating we blocked */
        }
    }
    return 0;
}

/* **************************************************************************** */
/**
 * Function to insert dtd task in PaRSEC
 *
 * In this function we track all the dependencies and create the DAG
 *
 */
void
parsec_insert_dtd_task(parsec_task_t *__this_task)
{
    if( PARSEC_TASKPOOL_TYPE_DTD != __this_task->taskpool->taskpool_type ) {
        parsec_fatal("Error! Taskpool is of incorrect type\n");
    }

    parsec_dtd_task_t *this_task = (parsec_dtd_task_t *)__this_task;
    const parsec_task_class_t *tc          =  this_task->super.task_class;
    parsec_dtd_taskpool_t *dtd_tp = (parsec_dtd_taskpool_t *)this_task->super.taskpool;

    int flow_index, satisfied_flow = 0, tile_op_type = 0, put_in_chain = 1;
    static int vpid = 0;
    parsec_dtd_tile_t *tile = NULL;

    /* Retaining runtime_task */
    parsec_taskpool_update_runtime_nbtask( this_task->super.taskpool, 1 );

    /* Retaining every remote_task */
    if( parsec_dtd_task_is_remote( this_task ) ) {
        parsec_dtd_remote_task_retain( this_task );
    }

    /* In the next segment we resolve the dependencies of each flow */
    for( flow_index = 0, tile = NULL, tile_op_type = 0; flow_index < tc->nb_flows; flow_index ++ ) {
        parsec_dtd_tile_user_t last_user, last_writer;
        tile = (FLOW_OF(this_task, flow_index))->tile;
        tile_op_type = (FLOW_OF(this_task, flow_index))->op_type;
        put_in_chain = 1;

        if(0 == dtd_tp->flow_set_flag[tc->task_class_id]) {
            /* Setting flow in function structure */
            parsec_dtd_set_flow_in_function(dtd_tp, this_task, tile_op_type, flow_index);
        }

        if( NULL == tile ) {
            satisfied_flow++;
            continue;
        }

        /* User has instructed us not to track this data */
        if( tile_op_type & DONT_TRACK ) {
            this_task->super.data[flow_index].data_in = tile->data_copy;
            satisfied_flow++;
            continue;
        }

        if( tile->arena_index == -1 ) {
            tile->arena_index = (tile_op_type & GET_REGION_INFO);
        }
        (FLOW_OF(this_task, flow_index))->arena_index = (tile_op_type & GET_REGION_INFO);

        /* Locking the last_user of the tile */
        parsec_dtd_last_user_lock( &(tile->last_user) );

        READ_FROM_TILE(last_user, tile->last_user);
        READ_FROM_TILE(last_writer, tile->last_writer);

        if( NULL == last_user.task && (this_task->rank != tile->rank || (tile_op_type & GET_OP_TYPE) == INPUT) ) {
            parsec_dtd_last_user_unlock( &(tile->last_user) );

            /* parentless */
            /* Create Fake output_task */
            parsec_dtd_taskpool_insert_task( this_task->super.taskpool,
                                             &fake_first_out_body, 0, "Fake_FIRST_OUT",
                                             PASSED_BY_REF, tile, INOUT | AFFINITY,
                                             PARSEC_DTD_ARG_END );

            parsec_dtd_last_user_lock( &(tile->last_user) );

            READ_FROM_TILE(last_user, tile->last_user);
            READ_FROM_TILE(last_writer, tile->last_writer);

            assert( (last_user.task == NULL) || ((FLOW_OF(last_writer.task, last_writer.flow_index))->tile == tile) );
        }

        if( INOUT == (tile_op_type & GET_OP_TYPE) || OUTPUT == (tile_op_type & GET_OP_TYPE) ) {
#if defined(PARSEC_PROF_TRACE)
            this_task->super.prof_info.id = tile->key;
#endif

            /* Setting the last_user info with info of this_task */
            tile->last_writer.task        = this_task;
            tile->last_writer.flow_index  = flow_index;
            tile->last_writer.op_type     = tile_op_type;
            tile->last_writer.alive       = TASK_IS_ALIVE;

            /* retaining every remote_write task */
            if( parsec_dtd_task_is_remote( this_task ) ) {
                parsec_dtd_remote_task_retain( this_task ); /* for every write remote_task */
                if( NULL != last_writer.task ) {
                    if( parsec_dtd_task_is_local( last_writer.task ) ) {
                        parsec_dtd_remote_task_retain( this_task ); /* everytime we have a remote_task as descendant of a local task */
                    }
                }
            }
        } else { /* considering everything else in INPUT */
            if( parsec_dtd_task_is_remote( this_task ) ) {
                if( NULL != last_writer.task ) {
                    if( parsec_dtd_task_is_remote( last_writer.task ) ) {
                        /* if both last writer and this task(read) is remote
                           we do not put them in the chain
                        */
                        put_in_chain = 0;
                    } else {
                        parsec_dtd_remote_task_retain( this_task );
                    }
                } else {
                    assert(0);
                }
            }
        }

        if( put_in_chain ) {
            /* Setting the last_user info with info of this_task */
            tile->last_user.task       = this_task;
            tile->last_user.flow_index = flow_index;
            tile->last_user.op_type    = tile_op_type;
            tile->last_user.alive      = TASK_IS_ALIVE;
        }

        /* Unlocking the last_user of the tile */
        parsec_dtd_last_user_unlock( &(tile->last_user) );

        /* TASK_IS_ALIVE indicates we have a parent */
        if(TASK_IS_ALIVE == last_user.alive) {
            parsec_dtd_set_parent(last_writer.task, last_writer.flow_index,
                                  this_task, flow_index, last_writer.op_type,
                                  tile_op_type);

            set_dependencies_for_function( (parsec_taskpool_t *)dtd_tp,
                                           (parsec_task_class_t*)(PARENT_OF(this_task, flow_index))->task->super.task_class,
                                           (parsec_task_class_t *)this_task->super.task_class,
                                            (PARENT_OF(this_task, flow_index))->flow_index, flow_index );

            if( put_in_chain ) {
                assert(NULL != last_user.task);
                parsec_dtd_set_descendant(last_user.task, last_user.flow_index,
                                          this_task, flow_index, last_user.op_type,
                                          tile_op_type, last_user.alive);
            }

            /* Are we using the same data multiple times for the same task? */
            if(last_user.task == this_task) {
                satisfied_flow += 1;
                this_task->super.data[flow_index].data_in = this_task->super.data[last_user.flow_index].data_in;
                /* We retain data for each flow of a task */
                if( this_task->super.data[last_user.flow_index].data_in != NULL ) {
                    parsec_dtd_retain_data_copy(this_task->super.data[last_user.flow_index].data_in);
                }

                /* What if we have the same task using the same data in different flows
                 * with the corresponding  operation type on the data : R then W, we are
                 * doomed and this is to not get doomed
                 */
                if(parsec_dtd_task_is_local(this_task)) {
                    /* Checking if a task uses same data on different
                     * flows in the order W, R ...
                     * If yes, we MARK the later flow, as the last flow
                     * needs to release ownership and it is not released
                     * in the case where the last flow is a R.
                     */
                    if( ( (last_user.op_type  & GET_OP_TYPE) == INOUT || (last_user.op_type  & GET_OP_TYPE) == OUTPUT ) && ((tile_op_type & GET_OP_TYPE) == INPUT) ) {
                        FLOW_OF(this_task, flow_index)->flags |= RELEASE_OWNERSHIP_SPECIAL;
                    } else if( (last_user.op_type  & GET_OP_TYPE) == INPUT && (tile_op_type & GET_OP_TYPE) == INPUT ) {
                        /* we unset flag for previous flow and set it for last one */
                        FLOW_OF(last_user.task, last_user.flow_index)->flags &= ~RELEASE_OWNERSHIP_SPECIAL;
                        FLOW_OF(this_task, flow_index)->flags |= RELEASE_OWNERSHIP_SPECIAL;
                    }

                    if( ((tile_op_type & GET_OP_TYPE) == OUTPUT || (tile_op_type & GET_OP_TYPE) == INOUT)
                        && (last_user.op_type & GET_OP_TYPE) == INPUT ) {

                        /* clearing bit set to track special release of ownership */
                        FLOW_OF(last_user.task, last_user.flow_index)->flags &= ~RELEASE_OWNERSHIP_SPECIAL;

                        if( this_task->super.data[flow_index].data_in != NULL ) {
                            (void)parsec_atomic_fetch_dec_int32( &this_task->super.data[flow_index].data_in->readers );
                        }
                    }
                }

                /* This will fail if a task has W -> R -> W on the same data */
                if( ((last_user.op_type & GET_OP_TYPE) == OUTPUT || (last_user.op_type & GET_OP_TYPE) == INOUT) ) {
                    if( parsec_dtd_task_is_local(this_task) ) {
                        parsec_dtd_release_local_task( this_task );
                    }
                }
            }
            assert( NULL != last_user.task );
        } else {  /* Have parent, but parent is not alive
                     We have to call iterate successor on the parent to activate this task
                   */
            if( last_user.task != NULL ) {
                parsec_dtd_set_parent(last_writer.task, last_writer.flow_index,
                                      this_task, flow_index, last_writer.op_type,
                                      tile_op_type);

                set_dependencies_for_function( (parsec_taskpool_t *)dtd_tp,
                                               (parsec_task_class_t *)(PARENT_OF(this_task, flow_index))->task->super.task_class,
                                               (parsec_task_class_t *)this_task->super.task_class,
                                               (PARENT_OF(this_task, flow_index))->flow_index, flow_index );

                /* There might be cases where the parent might have iterated it's successor
                 * while we are forming a task using same data in multiple flows. In those
                 * cases the task we are forming will never be enabled if it has an order of
                 * operation on the data as following: R, .... R, W. This takes care of those
                 * cases.
                 */
                if(last_user.task == this_task) {
                    if( (last_user.op_type & GET_OP_TYPE) == INPUT ) {
                        if( this_task->super.data[last_user.flow_index].data_in != NULL ) {
                            (void)parsec_atomic_fetch_dec_int32( &this_task->super.data[last_user.flow_index].data_in->readers );
                        }
                    }

                }

                /* we can avoid all the hash table crap if the last_writer is not alive */
                if( put_in_chain ) {
                    parsec_dtd_set_descendant((PARENT_OF(this_task, flow_index))->task, (PARENT_OF(this_task, flow_index))->flow_index,
                                              this_task, flow_index, (PARENT_OF(this_task, flow_index))->op_type,
                                              tile_op_type, last_user.alive);

                    parsec_dtd_task_t *parent_task = (PARENT_OF(this_task, flow_index))->task;
                    if( parsec_dtd_task_is_local(parent_task) || parsec_dtd_task_is_local(this_task) ) {
                        int action_mask = 0;
                        action_mask |= (1<<(PARENT_OF(this_task, flow_index))->flow_index);

                        parsec_execution_stream_t *es = dtd_tp->super.context->virtual_processes[0]->execution_streams[0];

                        if( parsec_dtd_task_is_local(parent_task) && parsec_dtd_task_is_remote(this_task) ) {
                            /* To make sure we do not release any remote data held by this task */
                            parsec_dtd_remote_task_retain(parent_task);
                        }
                        this_task->super.task_class->release_deps(es,
                                                   (parsec_task_t *)(PARENT_OF(this_task, flow_index))->task,
                                                   action_mask                         |
                                                   PARSEC_ACTION_SEND_REMOTE_DEPS      |
                                                   PARSEC_ACTION_SEND_INIT_REMOTE_DEPS |
                                                   PARSEC_ACTION_RELEASE_REMOTE_DEPS   |
                                                   PARSEC_ACTION_COMPLETE_LOCAL_TASK   |
                                                   PARSEC_ACTION_RELEASE_LOCAL_DEPS , NULL);
                        if( parsec_dtd_task_is_local(parent_task) && parsec_dtd_task_is_remote(this_task) ) {
                            parsec_dtd_release_local_task( parent_task );
                        }
                    } else {
                        if((tile_op_type & GET_OP_TYPE) == INPUT) {
                            parsec_dtd_last_user_lock( &(tile->last_user) );
                            tile->last_user.alive = TASK_IS_NOT_ALIVE;
                            parsec_dtd_last_user_unlock( &(tile->last_user) );
                        }
                    }
                }
            } else {
                if((tile_op_type & GET_OP_TYPE) == INPUT || (tile_op_type & GET_OP_TYPE) == INOUT) {
                    set_dependencies_for_function( (parsec_taskpool_t *)dtd_tp, NULL,
                                                   (parsec_task_class_t *)this_task->super.task_class,
                                                    0, flow_index );
                }
                this_task->super.data[flow_index].data_in = tile->data_copy;
                satisfied_flow += 1;
                if( tile->data_copy != NULL ) {
                    /* We are using this local data for the first time, let's retain it */
                    parsec_dtd_retain_data_copy(tile->data_copy);
                }
            }
        }

        if( INOUT == (tile_op_type & GET_OP_TYPE) || OUTPUT == (tile_op_type & GET_OP_TYPE) ) {
            if( NULL != last_writer.task ) {
                if( parsec_dtd_task_is_remote( last_writer.task ) ) {
                    /* releasing last writer every time writer is changed */
                    parsec_dtd_remote_task_release( last_writer.task );
                }
            }
        }
    }

    dtd_tp->flow_set_flag[tc->task_class_id] = 1;

    if( parsec_dtd_task_is_local(this_task) ) {/* Task is local */
        (void)parsec_atomic_fetch_inc_int32(&dtd_tp->super.nb_tasks);
        dtd_tp->local_task_inserted++;
        PARSEC_DEBUG_VERBOSE(parsec_dtd_dump_traversal_info, parsec_dtd_debug_output,
                             "Task generated -> %s %d rank %d\n", this_task->super.task_class->name, this_task->ht_item.key, this_task->rank);
    }

    /* Releasing every remote_task */
    if( parsec_dtd_task_is_remote( this_task ) ) {
        parsec_dtd_remote_task_release( this_task );
    }

    /* Increase the count of satisfied flows to counter-balance the increase in the
     * number of expected flows done during the task creation.  */
    satisfied_flow++;

#if defined(PARSEC_PROF_TRACE)
    if(parsec_dtd_profile_verbose)
        parsec_profiling_ts_trace(insert_task_trace_keyout, 0, dtd_tp->super.taskpool_id, NULL);
#endif

    if( parsec_dtd_task_is_local(this_task) ) {
        parsec_dtd_schedule_task_if_ready(satisfied_flow, this_task,
                                          dtd_tp, &vpid);
    }

    parsec_dtd_block_if_threshold_reached(dtd_tp, parsec_dtd_threshold_size);
}

int
parsec_dtd_iterator_arg_get_rank(int first_arg, void *tile,
                                 int tile_op_type, void *cb_data)
{
    (void)first_arg;
    parsec_dtd_common_args_t *common_args = (parsec_dtd_common_args_t *)cb_data;
    if( NULL != tile ) {
        if( (tile_op_type & AFFINITY) ) {
            if(common_args->rank == -1) {
                if( (tile_op_type & GET_OP_TYPE) == INPUT || (tile_op_type & GET_OP_TYPE) == INOUT || (tile_op_type & GET_OP_TYPE) == OUTPUT ) {
                    common_args->rank = ((parsec_dtd_tile_t *)tile)->rank;
                } else if((tile_op_type & GET_OP_TYPE) == VALUE) {
                    common_args->rank = *(int *)tile;
                    /* Warn user if rank passed is negative or
                     * more than total no of mpi process.
                     */
                    if(common_args->rank < 0 || common_args->rank >= common_args->dtd_tp->super.context->nb_nodes) {
                        parsec_warning("/!\\ Rank information passed to task is invalid, placing task in rank 0 /!\\.\n");
                    }
                }
            } else {
                parsec_warning("/!\\ Task is already placed, only the first use of AFFINITY flag is effective, others are ignored /!\\.\n");
            }
        }
    }

    if( (tile_op_type & GET_OP_TYPE) == INPUT || (tile_op_type & GET_OP_TYPE) == INOUT || (tile_op_type & GET_OP_TYPE) == OUTPUT ) {
        /* We create a new task class if the kernel is different and
         * if the same kernel uses different number of data
         */
        common_args->flow_count_of_template++;
        if( NULL != tile ) {
            if( !(tile_op_type & DONT_TRACK) ) {
                if( INOUT == (tile_op_type & GET_OP_TYPE) || OUTPUT == (tile_op_type & GET_OP_TYPE) ) {
                    common_args->write_flow_count++;
                }
            }
        }
    }
    return 1;
}

int
parsec_dtd_iterator_arg_get_size(int first_arg, void *tile,
                                 int tile_op_type, void *cb_data)
{
    (void)tile;
    parsec_dtd_common_args_t *common_args = (parsec_dtd_common_args_t *)cb_data;
    common_args->count_of_params_sent_by_user++;

    if((tile_op_type & GET_OP_TYPE) == VALUE || (tile_op_type & GET_OP_TYPE) == SCRATCH ||
       (tile_op_type & GET_OP_TYPE) == REF ) {
        common_args->size_of_params += first_arg;
    }

    return 1;
}

int
parsec_dtd_iterator_arg_set_param_local(int first_arg, void *tile,
                                        int tile_op_type, void *cb_data)
{
    (void)tile;
    parsec_dtd_common_args_t *common_args = (parsec_dtd_common_args_t *)cb_data;
    parsec_dtd_set_params_of_task(common_args->task, tile, tile_op_type,
                                  &common_args->flow_index, &common_args->current_val,
                                  common_args->current_param, first_arg);

    common_args->current_param->arg_size = first_arg;
    common_args->current_param->op_type  = tile_op_type;
    common_args->tmp_param               = common_args->current_param;
    common_args->current_param           = common_args->current_param + 1;
    common_args->tmp_param->next         = common_args->current_param;

    return 1;
}

int
parsec_dtd_iterator_arg_set_param_remote(int first_arg, void *tile,
                                int tile_op_type, void *cb_data)
{
    (void)tile;
    parsec_dtd_common_args_t *common_args = (parsec_dtd_common_args_t *)cb_data;

    if( (tile_op_type & GET_OP_TYPE) == INPUT || (tile_op_type & GET_OP_TYPE) == INOUT || (tile_op_type & GET_OP_TYPE) == OUTPUT ) {
        parsec_dtd_set_params_of_task( common_args->task, tile, tile_op_type,
                                       &common_args->flow_index, NULL,
                                       NULL, first_arg );
    }
    return 1;
}

/*
 * This function iterates over a va_list, whose end
 * is marked by PARSEC_DTD_ARG_END. It expects a set
 * of three arguments, first of which has to be an int.
 * A method is passed that is called for the set with
 * accompanying data that might be needed.
 */
int
parsec_dtd_arg_iterator(va_list args, parsec_dtd_arg_cb *cb, void *cb_data)
{
    /* We always expect three arguments to come a set */
    int first_arg, third_arg;
    void *second_arg;
    while(PARSEC_DTD_ARG_END != (first_arg = va_arg(args, int))) {
        second_arg = va_arg(args, void *);
        third_arg  = va_arg(args, int);
        cb(first_arg, second_arg, third_arg, cb_data);
    }
    return 1;
}

static inline parsec_task_t *
__parsec_dtd_taskpool_create_task(parsec_taskpool_t  *tp,
                                  parsec_dtd_funcptr_t *fpointer, int priority,
                                  const char *name_of_kernel, va_list args)
{
    parsec_dtd_taskpool_t *dtd_tp = (parsec_dtd_taskpool_t *)tp;

    if( tp->context == NULL ) {
        parsec_fatal( "Sorry! You can not insert task wihtout enqueuing the taskpool to parsec_context"
                      " first. Please make sure you call parsec_context_add_taskpool(parsec_context, taskpool) before"
                      " you try inserting task in PaRSEC\n" );
    }

    if( dtd_tp == NULL ) {
        parsec_fatal( "Wait! You need to pass a correct parsec taskpool in order to insert task. "
                      "Please use \"parsec_dtd_taskpool_new()\" to create new taskpool"
                      "and then try to insert task. Thank you\n" );
    }

    va_list args_for_size, args_for_rank;
    parsec_dtd_common_args_t common_args;

    common_args.rank = -1; common_args.write_flow_count = 1;
    common_args.flow_count_of_template = 0; common_args.dtd_tp = dtd_tp;
    common_args.count_of_params_sent_by_user = 0;
    common_args.size_of_params = 0; common_args.flow_index = 0;

#if defined(PARSEC_PROF_TRACE)
    if(parsec_dtd_profile_verbose)
        parsec_profiling_ts_trace(insert_task_trace_keyin, 0, dtd_tp->super.taskpool_id, NULL);
#endif

    /* extracting the rank of the task */
    va_copy(args_for_rank, args);
    parsec_dtd_arg_iterator(args_for_rank, parsec_dtd_iterator_arg_get_rank, (void*)&common_args);
    va_end(args_for_rank);

    uint64_t fkey = (uint64_t)(uintptr_t)fpointer + common_args.flow_count_of_template;
    /* Creating master function structures */
    /* Hash table lookup to check if the function structure exists or not */
    parsec_task_class_t *tc = (parsec_task_class_t *)
                                  parsec_dtd_find_task_class(dtd_tp, fkey);

    if( NULL == tc ) {
        va_copy(args_for_size, args);
        parsec_dtd_arg_iterator(args_for_size, parsec_dtd_iterator_arg_get_size, (void*)&common_args);
        va_end(args_for_size);

        tc = parsec_dtd_create_task_class(dtd_tp, fpointer, name_of_kernel,
                                          common_args.count_of_params_sent_by_user,
                                          common_args.size_of_params, common_args.flow_count_of_template);

#if defined(PARSEC_PROF_TRACE)
        parsec_dtd_add_profiling_info((parsec_taskpool_t *)dtd_tp, tc->task_class_id, name_of_kernel);
#endif /* defined(PARSEC_PROF_TRACE) */
    }

#if defined(DISTRIBUTED)
    if( tp->context->nb_nodes > 1 ) {
        if( (-1 == common_args.rank) && (common_args.write_flow_count > 1) ) {
            parsec_fatal( "You inserted a task with out indicating where the task should be executed(using AFFINITY flag)."
                          "This will result in executing this task on all nodes and the outcome might be not be what you want."
                          "So we are exiting for now. Please see the usage of AFFINITY flag.\n" );
        } else if( common_args.rank == -1 && common_args.write_flow_count == 1 ) {
            /* we have tasks with no real data as parameter so we are safe to execute it in each mpi process */
            common_args.rank = tp->context->my_rank;
        }
    } else {
        common_args.rank = 0;
    }
#else
    common_args.rank = 0;
#endif

    parsec_dtd_task_t *this_task = parsec_dtd_create_and_initialize_task(dtd_tp, tc, common_args.rank);
    this_task->super.priority = priority;
    common_args.task = this_task;

    if( parsec_dtd_task_is_local(this_task) ) {
        parsec_object_t *object = (parsec_object_t *)this_task;
        /* retaining the local task as many write flows as
         * it has and one to indicate when we have executed the task */
        (void)parsec_atomic_fetch_add_int32( &object->obj_reference_count, common_args.write_flow_count );

        common_args.tmp_param = NULL;

        /* Getting the pointer to allocated memory by mempool */
        common_args.head_of_param_list = GET_HEAD_OF_PARAM_LIST(this_task);
        common_args.current_param      = common_args.head_of_param_list;
        common_args.value_block        = GET_VALUE_BLOCK(common_args.head_of_param_list, ((parsec_dtd_task_class_t*)tc)->count_of_params);
        common_args.current_val        = common_args.value_block;

        parsec_dtd_arg_iterator(args, parsec_dtd_iterator_arg_set_param_local, (void*)&common_args);

        if( common_args.tmp_param != NULL )
            common_args.tmp_param->next = NULL;
    } else {
        parsec_dtd_arg_iterator(args, parsec_dtd_iterator_arg_set_param_remote, (void*)&common_args);
    }

#if defined(DISTRIBUTED)
    assert(this_task->rank != -1);
#endif

    return (parsec_task_t *)this_task;
}

/* **************************************************************************** */
/**
 * Function to insert task in PaRSEC
 *
 * Each time the user calls it a task is created with the
 * respective parameters the user has passed. For each task
 * class a structure known as "function" is created as well.
 * (e.g. for Cholesky 4 function structures are created for
 * each task class). The flow of data from each task to others
 * and all other dependencies are tracked from this function.
 *
 * @param[in,out]   __tp
 *                      DTD taskpool
 * @param[in]       fpointer
 *                      The pointer to the body of the task
 * @param[in]       priority
 *                      The priority of the task
 * @param[in]       ...
 *                      Variadic parameters of the task
 *
 * @ingroup         DTD_INTERFACE
 */
void
parsec_dtd_taskpool_insert_task(parsec_taskpool_t  *tp,
                                parsec_dtd_funcptr_t *fpointer, int priority,
                                const char *name_of_kernel, ...)
{
    va_list args;
    va_start(args, name_of_kernel);

    parsec_task_t *this_task = __parsec_dtd_taskpool_create_task(tp, fpointer, priority,
                                                                 name_of_kernel, args);
    va_end(args);

    if(NULL != this_task) {
        parsec_insert_dtd_task(this_task);
    } else {
        parsec_fatal("Unknow Error! Could not create task\n");
    }
}

parsec_task_t *
parsec_dtd_taskpool_create_task(parsec_taskpool_t  *tp,
                                parsec_dtd_funcptr_t *fpointer, int priority,
                                const char *name_of_kernel, ...)
{
    va_list args;
    va_start(args, name_of_kernel);

    parsec_task_t *this_task = __parsec_dtd_taskpool_create_task(tp, fpointer, priority,
                                                                 name_of_kernel, args);
    va_end(args);

    return this_task;
}

parsec_taskpool_t *
parsec_dtd_get_taskpool(parsec_task_t *this_task)
{
    return this_task->taskpool;
}
