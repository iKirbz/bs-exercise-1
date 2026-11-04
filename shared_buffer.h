/** @file shared_buffer.h
 * @brief shared buffer for generator and supervisor
 * @details hold solutions and positions for circular buffer
 * @author Anton Prem 12522014
 * @date 2026-10-27
 */

#ifndef SHARED_BUFFER_H
#define SHARED_BUFFER_H

#include "generator.h"
#include <semaphore.h>
#include <stdbool.h>

#define BUFFER_SOLUTION_CAPACITY 32

#define SHARED_BUFFER_NAME "/12522014_feedback_arcs"
#define SEM_FREE "/12522014_feedback_arcs_free"
#define SEM_USED "/12522014_feedback_arcs_used"
#define SEM_MUTEX "/12522014_feedback_arcs_mutex"

typedef enum {
  BUFFER_OPERATION_OK,
  BUFFER_STOP_REQUESTED,
  BUFFER_OPERATION_ERROR
} BufferOperationResult;

typedef struct {
  size_t read_pos;
  size_t write_pos;
  bool stop;
  Solution slots[BUFFER_SOLUTION_CAPACITY];
} SharedBuffer;

/** @brief read one solution from the circular buffer
 * @param program program name for error message
 * @param buffer shared circular buffer
 * @param mutex semaphore protecting buffer positions and slots
 * @param free_slots semaphore counting free slots
 * @param used_slots semaphore counting used slots
 * @param solution receives the read solution
 * @return true when a solution was read otherwise false
 */
bool shared_buffer_read(const char *program, SharedBuffer *buffer, sem_t *mutex,
                        sem_t *free_slots, sem_t *used_slots,
                        Solution *solution);

/** @brief write one solution to the circular buffer
 * @param program program name for error message
 * @param buffer shared circular buffer
 * @param mutex semaphore protecting buffer positions and slots
 * @param free_slots semaphore counting free slots
 * @param used_slots semaphore counting used slots
 * @param solution solution to write
 * @return operation result
 */
BufferOperationResult shared_buffer_write(const char *program,
                                          SharedBuffer *buffer, sem_t *mutex,
                                          sem_t *free_slots, sem_t *used_slots,
                                          const Solution *solution);

/** @brief set the shared stop flag
 * @param program program name for error message
 * @param buffer shared circular buffer
 * @param mutex semaphore protecting shared buffer state
 * @return true when stop flag was set otherwise false
 */
bool shared_buffer_request_stop(const char *program, SharedBuffer *buffer,
                                sem_t *mutex);

#endif
