#include "shared_buffer.h"
#include <fcntl.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char **argv) {
  int result = EXIT_FAILURE;
  int shmfd = -1;
  SharedBuffer *shared_buffer = MAP_FAILED;
  sem_t *sem_mutex = SEM_FAILED;
  sem_t *sem_free = SEM_FAILED;
  sem_t *sem_used = SEM_FAILED;
  bool shm_created = false;
  bool mutex_created = false;
  bool free_created = false;
  bool used_created = false;

  bool h_arg = false;
  char *o_arg = NULL;
  char *n_arg = NULL;
  char *w_arg = NULL;
  long delay = 0; // seconds

  int c;

  while ((c = getopt(argc, argv, "ho:n:w:")) != -1) {
    switch (c) {
    case 'h':
      h_arg = true;
      printf("h is given\n");
      break;
    case 'o':
      o_arg = optarg;
      printf("o is given with arg %s\n", o_arg);
      break;
    case 'n':
      n_arg = optarg;
      printf("n is given with arg %s\n", n_arg);
      break;
    case 'w':
      w_arg = optarg;
      char *end;
      delay = strtol(w_arg, &end, 10);
      printf("w is given with arg %s\n", w_arg);
      break;
    case '?': /* invalid option */
      fprintf(stderr,
              "usage: supervisor [-h] [-o outfile] [-n limit] [-w delay]");
      return EXIT_SUCCESS;
      break;
    default: /* should not occur */
      break;
    }
  }

  /*******************************************
   *  shared memory
   ***********************************************/
  shmfd = shm_open(SHARED_BUFFER_NAME, O_RDWR | O_EXCL | O_CREAT, 0600);
  if (shmfd == -1) {
    fprintf(stderr, "error creating shared memory: \n");
    perror("shm_open");
    goto cleanup;
  }

  shm_created = true;

  // set the size of the shared memory:
  if (ftruncate(shmfd, (off_t)sizeof(SharedBuffer)) == -1) {
    perror("ftruncate");
    goto cleanup;
  }

  // map shared memory object:
  shared_buffer = mmap(NULL, sizeof(SharedBuffer), PROT_READ | PROT_WRITE,
                       MAP_SHARED, shmfd, 0);

  if (shared_buffer == MAP_FAILED) {
    fprintf(stderr, "error mapping shared memory: \n");
    goto cleanup;
  }

  *shared_buffer = (SharedBuffer){0};

  if (close(shmfd) == -1) {
    perror("close shared memory fd");
    shmfd = -1;
    goto cleanup;
  }
  shmfd = -1;

  /*******************************************
   *  sem mutex creation
   ***********************************************/

  sem_mutex = sem_open(SEM_MUTEX, O_CREAT | O_EXCL, 0600, 1);
  if (sem_mutex == SEM_FAILED) {
    perror("sem_open mutex");
    goto cleanup;
  }
  mutex_created = true;

  /*******************************************
   *  sem free creation
   ***********************************************/

  sem_free =
      sem_open(SEM_FREE, O_CREAT | O_EXCL, 0600, BUFFER_SOLUTION_CAPACITY);
  if (sem_free == SEM_FAILED) {
    perror("sem_open free slots");
    goto cleanup;
  }
  free_created = true;

  /*******************************************
   *  sem used creation
   ***********************************************/
  sem_used = sem_open(SEM_USED, O_CREAT | O_EXCL, 0600, 0);
  if (sem_used == SEM_FAILED) {
    perror("sem_open used slots");
    goto cleanup;
  }
  used_created = true;

  sleep(delay);

  /*******************************************
   *  ready for work
   ***********************************************/
  Solution best = {{}, SIZE_MAX};

  while (true) {

    if (best.count == 0) {
      shared_buffer->stop = true;
      printf("graph is acyclic");
      break;
    }

    if (sem_wait(sem_used) == -1) {
      perror("sem_wait used");
      goto cleanup;
    }

    if (sem_wait(sem_mutex) == -1) {
      perror("sem_wait mutex");
      if (sem_post(sem_used) == -1) {
        perror("sem_post used after mutex wait failure");
      }
      goto cleanup;
    }

    Solution temp = shared_buffer->slots[shared_buffer->read_pos];

    shared_buffer->read_pos =
        (shared_buffer->read_pos + 1) % BUFFER_SOLUTION_CAPACITY;

    if (temp.count < best.count) {
      best = temp;
    }

    if (sem_post(sem_mutex) == -1) {
      perror("sem_post mutex");
      goto cleanup;
    }

    if (sem_post(sem_free) == -1) {
      perror("sem_post free");
      goto cleanup;
    }
  }

  result = EXIT_SUCCESS;

  /*******************************************
   *  cleanup
   ***********************************************/

cleanup:
  if (sem_mutex != SEM_FAILED && sem_close(sem_mutex) == -1) {
    perror("sem_close mutex");
    result = EXIT_FAILURE;
  }
  if (sem_free != SEM_FAILED && sem_close(sem_free) == -1) {
    perror("sem_close free slots");
    result = EXIT_FAILURE;
  }
  if (sem_used != SEM_FAILED && sem_close(sem_used) == -1) {
    perror("sem_close used slots");
    result = EXIT_FAILURE;
  }
  if (mutex_created && sem_unlink(SEM_MUTEX) == -1) {
    perror("sem_unlink mutex");
    result = EXIT_FAILURE;
  }
  if (free_created && sem_unlink(SEM_FREE) == -1) {
    perror("sem_unlink free slots");
    result = EXIT_FAILURE;
  }
  if (used_created && sem_unlink(SEM_USED) == -1) {
    perror("sem_unlink used slots");
    result = EXIT_FAILURE;
  }
  if (shared_buffer != MAP_FAILED &&
      munmap(shared_buffer, sizeof *shared_buffer) == -1) {
    perror("munmap shared memory");
    result = EXIT_FAILURE;
  }
  if (shmfd != -1 && close(shmfd) == -1) {
    perror("close shared memory descriptor");
    result = EXIT_FAILURE;
  }
  if (shm_created && shm_unlink(SHARED_BUFFER_NAME) == -1) {
    perror("shm_unlink");
    result = EXIT_FAILURE;
  }
  return result;
}
