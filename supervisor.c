/**
 * @file supervisor.c
 * @brief supervise generators and keep best feedback arc set
 * @details create shared resources read solutions and stop generators
 * @author Anton Prem, 12522014
 * @date 2026-10-27
 */

#include "shared_buffer.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static bool print_solution(FILE *stream, const Solution *solution);

static volatile sig_atomic_t stop_requested = 0;

/** @brief set stop flag when a signal arrives
 * @param signal_number received signal number
 */
static void handle_signal(int signal_number) {
  (void)signal_number;
  stop_requested = 1;
}

/** @brief run the supervisor
 * @param argc number of command line arguments
 * @param argv command line arguments
 * @return EXIT_SUCCESS when supervision ends normally otherwise EXIT_FAILURE
 */
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
  FILE *outfile = NULL;

  char *o_arg = NULL;
  long solution_limit = -1;
  long delay = 0; // delay in seconds

  int c;

  /*******************************************
   *  signal handler setup
   ***********************************************/
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = handle_signal;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGINT, &sa, NULL) == -1 ||
      sigaction(SIGTERM, &sa, NULL) == -1) {
    fprintf(stderr, "%s: sigaction: %s\n", argv[0], strerror(errno));
    return EXIT_FAILURE;
  }

  /*******************************************
   *  argument parsing
   ***********************************************/

  opterr = 0;
  while ((c = getopt(argc, argv, "ho:n:w:")) != -1) {
    switch (c) {
    case 'h':
      printf("Usage: %s [-h] [-o outfile] [-n limit] [-w delay]\n", argv[0]);
      return EXIT_SUCCESS;
    case 'o':
      o_arg = optarg;
      break;
    case 'n': {
      char *end;
      long parsed_limit;
      errno = 0;
      parsed_limit = strtol(optarg, &end, 10);
      if (optarg[0] == '\0' || *end != '\0' || errno == ERANGE ||
          parsed_limit < 0) {
        fprintf(stderr, "%s: invalid solution limit: %s\n", argv[0], optarg);
        fprintf(stderr, "Usage: %s [-h] [-o outfile] [-n limit] [-w delay]\n",
                argv[0]);
        return EXIT_FAILURE;
      }
      solution_limit = parsed_limit;
    } break;
    case 'w': {
      char *end;
      errno = 0;
      delay = strtol(optarg, &end, 10);
      if (optarg[0] == '\0' || *end != '\0' || errno == ERANGE || delay < 0 ||
          (unsigned long)delay > UINT_MAX) {
        fprintf(stderr, "%s: invalid delay: %s\n", argv[0], optarg);
        fprintf(stderr, "Usage: %s [-h] [-o outfile] [-n limit] [-w delay]\n",
                argv[0]);
        return EXIT_FAILURE;
      }
    } break;
    case '?': /* invalid option */
      fprintf(stderr, "Usage: %s [-h] [-o outfile] [-n limit] [-w delay]\n",
              argv[0]);
      return EXIT_FAILURE;
    default: /* should not occur */
      break;
    }
  }

  if (optind != argc) {
    fprintf(stderr, "Usage: %s [-h] [-o outfile] [-n limit] [-w delay]\n",
            argv[0]);
    return EXIT_FAILURE;
  }

  if (o_arg != NULL) {
    outfile = fopen(o_arg, "w");
    if (outfile == NULL) {
      fprintf(stderr, "%s: fopen output file: %s\n", argv[0], strerror(errno));
      goto cleanup;
    }
  }

  /*******************************************
   *  shared memory
   ***********************************************/
  shmfd = shm_open(SHARED_BUFFER_NAME, O_RDWR | O_EXCL | O_CREAT, 0600);
  if (shmfd == -1) {
    fprintf(stderr, "%s: shm_open shared memory: %s\n", argv[0],
            strerror(errno));
    goto cleanup;
  }

  shm_created = true;

  // set the size of the shared memory:
  if (ftruncate(shmfd, (off_t)sizeof(SharedBuffer)) == -1) {
    fprintf(stderr, "%s: ftruncate shared memory: %s\n", argv[0],
            strerror(errno));
    goto cleanup;
  }

  // map shared memory object:
  shared_buffer = mmap(NULL, sizeof(SharedBuffer), PROT_READ | PROT_WRITE,
                       MAP_SHARED, shmfd, 0);

  if (shared_buffer == MAP_FAILED) {
    fprintf(stderr, "%s: mmap shared memory: %s\n", argv[0], strerror(errno));
    goto cleanup;
  }

  *shared_buffer = (SharedBuffer){0};

  if (close(shmfd) == -1) {
    fprintf(stderr, "%s: close shared memory descriptor: %s\n", argv[0],
            strerror(errno));
    shmfd = -1;
    goto cleanup;
  }
  shmfd = -1;

  /*******************************************
   *  sem mutex creation
   ***********************************************/

  sem_mutex = sem_open(SEM_MUTEX, O_CREAT | O_EXCL, 0600, 1);
  if (sem_mutex == SEM_FAILED) {
    fprintf(stderr, "%s: sem_open mutex: %s\n", argv[0], strerror(errno));
    goto cleanup;
  }
  mutex_created = true;

  /*******************************************
   *  sem free creation
   ***********************************************/

  sem_free =
      sem_open(SEM_FREE, O_CREAT | O_EXCL, 0600, BUFFER_SOLUTION_CAPACITY);
  if (sem_free == SEM_FAILED) {
    fprintf(stderr, "%s: sem_open free slots: %s\n", argv[0], strerror(errno));
    goto cleanup;
  }
  free_created = true;

  /*******************************************
   *  sem used creation
   ***********************************************/
  sem_used = sem_open(SEM_USED, O_CREAT | O_EXCL, 0600, 0);
  if (sem_used == SEM_FAILED) {
    fprintf(stderr, "%s: sem_open used slots: %s\n", argv[0], strerror(errno));
    goto cleanup;
  }
  used_created = true;

  sleep(delay);

  /*******************************************
   *  ready for work
   ***********************************************/
  Solution best = {.count = SIZE_MAX};
  bool graph_acyclic = false;
  long solutions_read = 0;

  while (!stop_requested && !graph_acyclic) {
    Solution temp = {0};
    if (!shared_buffer_read(argv[0], shared_buffer, sem_mutex, sem_free,
                            sem_used, &temp)) {
      if (stop_requested) {
        break;
      }
      goto cleanup;
    }
    solutions_read++;

    if (temp.count < best.count) {
      best = temp;
      if (!print_solution(stderr, &best) ||
          (outfile != NULL && !print_solution(outfile, &best))) {
        fprintf(stderr, "%s: writing solution output failed\n", argv[0]);
        goto cleanup;
      }
    }
    if (temp.count == 0) {
      graph_acyclic = true;
    }
    if (solution_limit >= 0 && solutions_read > solution_limit) {
      break;
    }
  }

  if (!shared_buffer_request_stop(argv[0], shared_buffer, sem_mutex)) {
    goto cleanup;
  }
  if (graph_acyclic) {
    printf("The graph is acyclic!\n");
    if (outfile != NULL) {
      if (fprintf(outfile, "The graph is acyclic!\n") < 0) {
        fprintf(stderr, "%s: writing output file failed\n", argv[0]);
        goto cleanup;
      }
    }
  } else if (best.count != SIZE_MAX) {
    printf("The graph might not be acyclic, best solution removes %zu edges.\n",
           best.count);
    if (outfile != NULL) {
      if (fprintf(outfile,
                  "The graph might not be acyclic, best solution removes %zu "
                  "edges.\n",
                  best.count) < 0) {
        fprintf(stderr, "%s: writing output file failed\n", argv[0]);
        goto cleanup;
      }
    }
  }
  result = EXIT_SUCCESS;

  /*******************************************
   *  cleanup
   ***********************************************/

cleanup:
  if (sem_mutex != SEM_FAILED && sem_close(sem_mutex) == -1) {
    fprintf(stderr, "%s: sem_close mutex: %s\n", argv[0], strerror(errno));
    result = EXIT_FAILURE;
  }
  if (sem_free != SEM_FAILED && sem_close(sem_free) == -1) {
    fprintf(stderr, "%s: sem_close free slots semaphore: %s\n", argv[0],
            strerror(errno));
    result = EXIT_FAILURE;
  }
  if (sem_used != SEM_FAILED && sem_close(sem_used) == -1) {
    fprintf(stderr, "%s: sem_close used slots semaphore: %s\n", argv[0],
            strerror(errno));
    result = EXIT_FAILURE;
  }
  if (mutex_created && sem_unlink(SEM_MUTEX) == -1) {
    fprintf(stderr, "%s: sem_unlink mutex: %s\n", argv[0], strerror(errno));
    result = EXIT_FAILURE;
  }
  if (free_created && sem_unlink(SEM_FREE) == -1) {
    fprintf(stderr, "%s: sem_unlink free slots: %s\n", argv[0],
            strerror(errno));
    result = EXIT_FAILURE;
  }
  if (used_created && sem_unlink(SEM_USED) == -1) {
    fprintf(stderr, "%s: sem_unlink used slots: %s\n", argv[0],
            strerror(errno));
    result = EXIT_FAILURE;
  }
  if (shared_buffer != MAP_FAILED &&
      munmap(shared_buffer, sizeof *shared_buffer) == -1) {
    fprintf(stderr, "%s: munmap shared memory: %s\n", argv[0], strerror(errno));
    result = EXIT_FAILURE;
  }
  if (shmfd != -1 && close(shmfd) == -1) {
    fprintf(stderr, "%s: close shared memory descriptor: %s\n", argv[0],
            strerror(errno));
    result = EXIT_FAILURE;
  }
  if (shm_created && shm_unlink(SHARED_BUFFER_NAME) == -1) {
    fprintf(stderr, "%s: shm_unlink shared memory: %s\n", argv[0],
            strerror(errno));
    result = EXIT_FAILURE;
  }
  if (outfile != NULL && fclose(outfile) == EOF) {
    fprintf(stderr, "%s: fclose output file: %s\n", argv[0], strerror(errno));
    result = EXIT_FAILURE;
  }
  return result;
}

/** @brief print feedback arc set in assignment format
 * @param stream output stream
 * @param solution solution to print
 * @return true when output succeeded otherwise false
 */
static bool print_solution(FILE *stream, const Solution *solution) {
  if (fprintf(stream, "Solution with %zu edges:", solution->count) < 0) {
    return false;
  }
  for (size_t i = 0; i < solution->count; i++) {
    if (fprintf(stream, " %d-%d", solution->edges[i].from,
                solution->edges[i].to) < 0) {
      return false;
    }
  }
  return fprintf(stream, "\n") >= 0;
}
