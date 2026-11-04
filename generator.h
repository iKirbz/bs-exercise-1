/**
 * @file generator.h
 * @brief data types for feedback arc set solutions
 * @details define edges and solution size used by generator and supervisor
 * @author Anton Prem, 12522014
 * @date 2026-10-27
 */

#ifndef GENERATOR_H
#define GENERATOR_H

#include <stddef.h>

#define MAX_FAS_EDGES 8

typedef struct {
  int from;
  int to;
} Edge;

typedef struct {
  Edge edges[MAX_FAS_EDGES];
  size_t count;
} Solution;

#endif
