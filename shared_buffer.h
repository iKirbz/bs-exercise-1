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
