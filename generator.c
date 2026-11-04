/**
 * @file generator.c
 * @brief generates feedback arc set for a directed graph
 * @details parses graph edges from cli arguments, shuffles the
 *          vertices, and selects edges that point backward
 * @author Anton Prem, 12522014
 * @date 2026-10-27
 */

#include "generator.h"
#include "shared_buffer.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define INITIAL_VERTEX_CAPACITY 1
#define INITIAL_EDGE_CAPACITY 1
#define INITIAL_FAS_EDGE_CAPACITY 1

typedef struct {
  int *data;
  size_t count;
  size_t capacity;
} VertexArray;

typedef struct {
  Edge *data;
  size_t count;
  size_t capacity;
} EdgeArray;

typedef struct {
  VertexArray vertices;
  EdgeArray edges;
} Graph;

static bool parse_edge(const char *text, Edge *edge);

static bool build_graph_from_args(int argc, char **argv, Graph *graph);

static bool grow_vertices(VertexArray *vertices);

static bool add_vertex(Graph *graph, int vertex);

static bool grow_edges(EdgeArray *edges);

static bool add_edge(EdgeArray *edges, Edge edge);

static void shuffle(VertexArray *vertices);

static bool calculate_fas(const Graph *graph, EdgeArray *fas_edges);

/** @brief build the graph and generate feedback arc sets
 * @param argc number of command-line arguments
 * @param argv the command-line arguments
 * @return EXIT_SUCCESS if it worked otherwise EXIT_FAILURE
 */
int main(int argc, char **argv) {
  int shmfd = -1;
  SharedBuffer *shared_buffer = MAP_FAILED;
  sem_t *sem_mutex = SEM_FAILED;
  sem_t *sem_free = SEM_FAILED;
  sem_t *sem_used = SEM_FAILED;
  int result = EXIT_FAILURE;

  Graph graph = {
      {NULL, 0, INITIAL_VERTEX_CAPACITY},
      {NULL, 0, INITIAL_EDGE_CAPACITY},
  };
  EdgeArray fas_edges = {NULL, 0, INITIAL_FAS_EDGE_CAPACITY};

  /* parse arguments and build the graph */
  if (!build_graph_from_args(argc, argv, &graph)) {
    goto cleanup;
  }

  /*******************************************
   *  shared memory
   ***********************************************/

  shmfd = shm_open(SHARED_BUFFER_NAME, O_RDWR);
  if (shmfd == -1) {
    fprintf(stderr, "%s: shm_open shared memory: %s\n", argv[0],
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

  if (close(shmfd) == -1) {
    fprintf(stderr, "%s: close shared memory descriptor: %s\n", argv[0],
            strerror(errno));
    shmfd = -1;
    goto cleanup;
  }
  shmfd = -1;

  /*******************************************
   *  sem mutex, free, used
   ***********************************************/

  sem_mutex = sem_open(SEM_MUTEX, 0);
  if (sem_mutex == SEM_FAILED) {
    fprintf(stderr, "%s: sem_open mutex: %s\n", argv[0], strerror(errno));
    goto cleanup;
  }

  sem_free = sem_open(SEM_FREE, 0);
  if (sem_free == SEM_FAILED) {
    fprintf(stderr, "%s: sem_open free slots: %s\n", argv[0], strerror(errno));
    goto cleanup;
  }

  sem_used = sem_open(SEM_USED, 0);
  if (sem_used == SEM_FAILED) {
    fprintf(stderr, "%s: sem_open used slots: %s\n", argv[0], strerror(errno));
    goto cleanup;
  }

  /*******************************************
   *  ready for work
   ***********************************************/

  srand((unsigned)time(NULL));

  while (true) {

    shuffle(&graph.vertices);

#ifdef DEBUG
    printf("Shuffled vertices:");
    for (size_t i = 0; i < graph.vertices.count; i++) {
      printf(" %d", graph.vertices.data[i]);
    }
    printf("\n");
#endif

    /* calculate feedback arc set */
    if (!calculate_fas(&graph, &fas_edges)) {
      fprintf(stderr, "%s: could not calculate feedback arc set\n", argv[0]);
      goto cleanup;
    }
#ifdef DEBUG
    printf("FAS contains %zu edges\n", fas_edges.count);
#endif

    if (fas_edges.count > MAX_FAS_EDGES) {
      continue;
    }

    Solution s = {0};
    s.count = fas_edges.count;

    for (size_t i = 0; i < s.count; i++) {
      s.edges[i] = fas_edges.data[i];
    }
    if (sem_wait(sem_free) == -1) {
      fprintf(stderr, "%s: sem_wait free slots: %s\n", argv[0],
              strerror(errno));
      goto cleanup;
    }

    if (sem_wait(sem_mutex) == -1) {
      fprintf(stderr, "%s: sem_wait mutex: %s\n", argv[0], strerror(errno));
      if (sem_post(sem_free) == -1) {
        fprintf(stderr,
                "%s: sem_post free slots after mutex wait failure: %s\n",
                argv[0], strerror(errno));
      }
      goto cleanup;
    }

    if (shared_buffer->stop) {
      if (sem_post(sem_mutex) == -1) {
        fprintf(stderr, "%s: sem_post mutex after stop check: %s\n", argv[0],
                strerror(errno));
        if (sem_post(sem_free) == -1) {
          fprintf(stderr, "%s: sem_post free slots after stop check: %s\n",
                  argv[0], strerror(errno));
        }
        goto cleanup;
      }
      if (sem_post(sem_free) == -1) {
        fprintf(stderr, "%s: sem_post free slots after stop check: %s\n",
                argv[0], strerror(errno));
        goto cleanup;
      }
      break;
    }

    shared_buffer->slots[shared_buffer->write_pos] = s;
    shared_buffer->write_pos =
        (shared_buffer->write_pos + 1) % BUFFER_SOLUTION_CAPACITY;

    if (sem_post(sem_mutex) == -1) {
      fprintf(stderr, "%s: sem_post mutex: %s\n", argv[0], strerror(errno));
      goto cleanup;
    }

    if (sem_post(sem_used) == -1) {
      fprintf(stderr, "%s: sem_post used slots: %s\n", argv[0],
              strerror(errno));
      goto cleanup;
    }
  }

  /* return success and free resources */
  result = EXIT_SUCCESS;

cleanup:
  free(graph.vertices.data);
  free(graph.edges.data);
  graph = (Graph){0};

  free(fas_edges.data);

  if (shared_buffer != MAP_FAILED &&
      munmap(shared_buffer, sizeof *shared_buffer) == -1) {
    fprintf(stderr, "%s: munmap shared memory: %s\n", argv[0], strerror(errno));
    result = EXIT_FAILURE;
  }
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
  return result;
}

/** @brief read edge arguments and build the graph
 * @param argc number of command-line arguments
 * @param argv the command-line arguments
 * @param graph gets the vertices and edges
 * @return true if it worked otherwise false
 * @details prints an error for bad input or memory failure
 */
static bool build_graph_from_args(int argc, char **argv, Graph *graph) {
  if (argc < 2) {
    fprintf(stderr, "%s: no edges given\n", argv[0]);
    return false;
  }

  graph->vertices.data =
      malloc(graph->vertices.capacity * sizeof *graph->vertices.data);
  graph->edges.data = malloc(graph->edges.capacity * sizeof *graph->edges.data);
  if (graph->vertices.data == NULL || graph->edges.data == NULL) {
    fprintf(stderr, "%s: could not allocate graph storage\n", argv[0]);
    return false;
  }

  for (int i = 1; i < argc; i++) {
    Edge edge;

    if (!parse_edge(argv[i], &edge)) {
      fprintf(stderr, "%s: invalid edge: %s\n", argv[0], argv[i]);
      return false;
    }

    if (!add_edge(&graph->edges, edge) || !add_vertex(graph, edge.from) ||
        !add_vertex(graph, edge.to)) {
      fprintf(stderr, "%s: could not store graph data\n", argv[0]);
      return false;
    }

#ifdef DEBUG
    printf("Edge: %d -> %d\n", edge.from, edge.to);
#endif
  }

  return true;
}

/** @brief parse one edge string
 * @param start edge text for example `3-4`
 * @param edge gets the two vertex numbers
 * @return true if the text is valid otherwise false
 */
static bool parse_edge(const char *start, Edge *edge) {
  char *end;
  long from;
  long to;

  if (start == NULL || edge == NULL) {
    return false;
  }

  errno = 0;
  from = strtol(start, &end, 10);
  // check if number is empty or too large for long and a positive integer
  // there must be a - behind the first number
  if (end == start || errno == ERANGE || from < 0 || from > INT_MAX ||
      *end != '-') {
    return false;
  }

  start = end + 1;
  errno = 0;
  to = strtol(start, &end, 10);
  // check if number is empty or too large for long and a positive integer
  // the second number must end with \0
  if (end == start || errno == ERANGE || to < 0 || to > INT_MAX ||
      *end != '\0') {
    return false;
  }

  edge->from = (int)from;
  edge->to = (int)to;
  return true;
}

/** @brief make room for more vertices
 * @param vertices the vertex array to grow
 * @return true if memory was added otherwise false
 */
static bool grow_vertices(VertexArray *vertices) {
  size_t new_capacity = vertices->capacity * 2;
  int *new_data =
      realloc(vertices->data, new_capacity * sizeof *vertices->data);
  if (new_data == NULL) {
    return false;
  }

#ifdef DEBUG
  printf("growing vertex array capacity from %zu to %zu\n", vertices->capacity,
         new_capacity);
#endif

  vertices->data = new_data;
  vertices->capacity = new_capacity;
  return true;
}

/** @brief add a vertex if it is not already in the graph
 * @param graph the graph to update
 * @param vertex the vertex number to add
 * @return true if it is present or was added otherwise false
 */
static bool add_vertex(Graph *graph, int vertex) {
  for (size_t i = 0; i < graph->vertices.count; i++) {
    if (graph->vertices.data[i] == vertex) {
      return true;
    }
  }

  // if there is no free space double the capacity
  if (graph->vertices.count == graph->vertices.capacity &&
      !grow_vertices(&graph->vertices)) {
    return false;
  }

  graph->vertices.data[graph->vertices.count] = vertex;
  graph->vertices.count++;
  return true;
}

/** @brief make room for more edges
 * @param edges the edge array to grow
 * @return true if memory was added otherwise false
 */
static bool grow_edges(EdgeArray *edges) {
  size_t new_capacity = edges->capacity * 2;
  Edge *new_data = realloc(edges->data, new_capacity * sizeof *edges->data);
  if (new_data == NULL) {
    return false;
  }

#ifdef DEBUG
  printf("growing edge array capacity from %zu to %zu\n", edges->capacity,
         new_capacity);
#endif

  edges->data = new_data;
  edges->capacity = new_capacity;
  return true;
}

/** @brief add an edge to an edge array
 * @param edges the array to update
 * @param edge the edge to add
 * @return true if it was added otherwise false
 */
static bool add_edge(EdgeArray *edges, Edge edge) {
  if (edges->count == edges->capacity && !grow_edges(edges)) {
    return false;
  }

  edges->data[edges->count] = edge;
  edges->count++;
  return true;
}

/** @brief shuffle vertices with fisher-yates
 * @param vertices the array to shuffle
 * @return nothing
 * @details uses the random number generator seeded in main
 */
static void shuffle(VertexArray *vertices) {
  for (size_t right = vertices->count; right > 1; right--) {
    size_t random_position = (size_t)rand() % right;
    int temp = vertices->data[random_position];

    vertices->data[random_position] = vertices->data[right - 1];
    vertices->data[right - 1] = temp;
  }
}

/** @brief find edges that point backward in the vertex order
 * @param graph the graph to check
 * @param fas_edges gets the edges to remove
 * @return true if it worked otherwise false
 */
static bool calculate_fas(const Graph *graph, EdgeArray *fas_edges) {
  if (fas_edges->data == NULL) {
    fas_edges->data = malloc(fas_edges->capacity * sizeof *fas_edges->data);
    if (fas_edges->data == NULL) {
      return false;
    }
  }
  fas_edges->count = 0;

  for (size_t i = 0; i < graph->edges.count; i++) {
    Edge edge = graph->edges.data[i];

    /* the edge points backward if the to vertex appears before the from
     * vertex */
    for (size_t j = 0; j < graph->vertices.count; j++) {
      int vertex = graph->vertices.data[j];

      if (vertex == edge.to) {
        if (!add_edge(fas_edges, edge)) {
          return false;
        }
        break;
      }
      if (vertex == edge.from) {
        break;
      }
    }
  }

  return true;
}
