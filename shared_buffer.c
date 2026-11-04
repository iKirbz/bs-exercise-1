/**
 * @file shared_buffer.c
 * @brief circular buffer operations for generator and supervisor
 * @details handle semaphore synchronization and buffer positions
 * @author Anton Prem, 12522014
 * @date 2026-10-27
 */

#include "shared_buffer.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

BufferReadResult shared_buffer_read(const char *program, SharedBuffer *buffer,
                                    sem_t *mutex, sem_t *free_slots,
                                    sem_t *used_slots, Solution *solution) {
  if (sem_wait(used_slots) == -1) {
    if (errno == EINTR)
      return BUFFER_READ_INTERRUPTED;
    fprintf(stderr, "%s: sem_wait used slots: %s\n", program, strerror(errno));
    return BUFFER_READ_ERROR;
  }
  if (sem_wait(mutex) == -1) {
    bool interrupted = errno == EINTR;
    if (interrupted) {
      if (sem_post(used_slots) == -1) {
        fprintf(stderr,
                "%s: sem_post used slots after interrupted mutex wait: %s\n",
                program, strerror(errno));
        return BUFFER_READ_ERROR;
      }
      return BUFFER_READ_INTERRUPTED;
    }
    fprintf(stderr, "%s: sem_wait mutex: %s\n", program, strerror(errno));
    if (sem_post(used_slots) == -1)
      fprintf(stderr, "%s: sem_post used slots: %s\n", program,
              strerror(errno));
    return BUFFER_READ_ERROR;
  }
  *solution = buffer->slots[buffer->read_pos];
  buffer->read_pos = (buffer->read_pos + 1) % BUFFER_SOLUTION_CAPACITY;
  if (sem_post(mutex) == -1) {
    fprintf(stderr, "%s: sem_post mutex: %s\n", program, strerror(errno));
    return BUFFER_READ_ERROR;
  }
  if (sem_post(free_slots) == -1) {
    fprintf(stderr, "%s: sem_post free slots: %s\n", program, strerror(errno));
    return BUFFER_READ_ERROR;
  }
  return BUFFER_READ_SUCCESS;
}

BufferOperationResult shared_buffer_write(const char *program,
                                          SharedBuffer *buffer, sem_t *mutex,
                                          sem_t *free_slots, sem_t *used_slots,
                                          const Solution *solution) {
  if (sem_wait(free_slots) == -1) {
    fprintf(stderr, "%s: sem_wait free slots: %s\n", program, strerror(errno));
    return BUFFER_OPERATION_ERROR;
  }
  if (sem_wait(mutex) == -1) {
    fprintf(stderr, "%s: sem_wait mutex: %s\n", program, strerror(errno));
    if (sem_post(free_slots) == -1)
      fprintf(stderr, "%s: sem_post free slots: %s\n", program,
              strerror(errno));
    return BUFFER_OPERATION_ERROR;
  }
  if (buffer->stop) {
    if (sem_post(mutex) == -1) {
      fprintf(stderr, "%s: sem_post mutex after stop check: %s\n", program,
              strerror(errno));
      if (sem_post(free_slots) == -1) {
        fprintf(stderr, "%s: sem_post free slots after stop check: %s\n",
                program, strerror(errno));
      }
      return BUFFER_OPERATION_ERROR;
    }
    if (sem_post(free_slots) == -1) {
      fprintf(stderr, "%s: sem_post free slots after stop check: %s\n", program,
              strerror(errno));
      return BUFFER_OPERATION_ERROR;
    }
    return BUFFER_STOP_REQUESTED;
  }
  buffer->slots[buffer->write_pos] = *solution;
  buffer->write_pos = (buffer->write_pos + 1) % BUFFER_SOLUTION_CAPACITY;
  if (sem_post(mutex) == -1) {
    fprintf(stderr, "%s: sem_post mutex: %s\n", program, strerror(errno));
    return BUFFER_OPERATION_ERROR;
  }
  if (sem_post(used_slots) == -1) {
    fprintf(stderr, "%s: sem_post used slots: %s\n", program, strerror(errno));
    return BUFFER_OPERATION_ERROR;
  }
  return BUFFER_OPERATION_OK;
}

bool shared_buffer_request_stop(const char *program, SharedBuffer *buffer,
                                sem_t *mutex, sem_t *free_slots) {
  if (sem_wait(mutex) == -1) {
    fprintf(stderr, "%s: sem_wait mutex while setting stop flag: %s\n", program,
            strerror(errno));
    return false;
  }
  buffer->stop = true;
  if (sem_post(mutex) == -1) {
    fprintf(stderr, "%s: sem_post mutex after setting stop flag: %s\n", program,
            strerror(errno));
    return false;
  }
  // wake a generator writing thats blocked on a full buffer
  if (sem_post(free_slots) == -1) {
    fprintf(stderr, "%s: sem_post free slots while requesting stop: %s\n",
            program, strerror(errno));
    return false;
  }
  return true;
}
