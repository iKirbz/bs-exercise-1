/** @file shared_buffer.h
 * @brief shared buffer for generator and supervisor
 * @details hold solutions and positions for circular buffer
 * @author Anton Prem 12522014
 * @date 2026-10-27
 */

#ifndef SHARED_BUFFER_H
#define SHARED_BUFFER_H

#include "generator.h"
#include <stdbool.h>

#define BUFFER_SOLUTION_CAPACITY 32

#define SHARED_BUFFER_NAME "/12522014_feedback_arcs"
#define SEM_FREE "/12522014_feedback_arcs_free"
#define SEM_USED "/12522014_feedback_arcs_used"
#define SEM_MUTEX "/12522014_feedback_arcs_mutex"

typedef struct {
  size_t read_pos;
  size_t write_pos;
  bool stop;
  Solution slots[BUFFER_SOLUTION_CAPACITY];
} SharedBuffer;

#endif
