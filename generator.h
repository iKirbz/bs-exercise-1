/**
 * @file generator.h
 * @brief generates feedback arc set for a directed graph
 * @details parses graph edges from cli arguments, shuffles the
 *          vertices, and selects edges that point backward
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
