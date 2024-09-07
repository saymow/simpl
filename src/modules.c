
#include "modules.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "utils.h"

// Auxilliary struct for graph traversal
// todo: implement hashset for O(1) lookup perfomance
typedef struct {
  ModuleNode** list;
  int count;
  int capacity;
} SeenNodes;

// Auxilliary struct for graph traversal
typedef struct {
  int count;
  int capacity;
  ModuleNode** list;
} NodesStack;

void initModules(Modules* modules, const char* absPath);

bool findModuleNode(Modules* modules, ModuleNode* origin, ModuleNode** node,
                    const char* absPath);

void createModuleNode(ModuleNode** node, const char* absPath);

void createDependency(ModuleNode* origin, ModuleNode* target);

void resolveModuleNode(ModuleNode* node, ObjModule* module);

static bool searchNode(Modules* modules, module_id_t moduleId,
                       ModuleNode** responseNode);

void freeModules(Modules* modules);

// Initialize node stack structure
static void initNodesStack(NodesStack* nodesStack);

// Push node to stack
static void pushNodesStack(NodesStack* nodesStack, ModuleNode* moduleNode);

// Push node to from stack
static ModuleNode* popNodesStack(NodesStack* nodesStack);

// Free node stack structure
static void freeNodesStack(NodesStack* nodesStack);

// Initialize node stack structure
static void initSeenNodes(SeenNodes* seenNodes);

// Insert node to seen nodes
static void insertSeenNodes(SeenNodes* seenNodes, ModuleNode* moduleNode);

// Check if node exists on seen nodes
static bool hasSeenNode(SeenNodes* seenNodes, ModuleNode* moduleNode);

// Free seen nodes
static void freeSeenNodes(SeenNodes* seenNodes);

// Allocate module node
static ModuleNode* allocateNode(module_id_t id);

// Free module node
static void freeNode(ModuleNode* node);

static void initNodesStack(NodesStack* nodesStack) {
  nodesStack->count = 0;
  nodesStack->capacity = 0;
  nodesStack->list = NULL;
}

static void pushNodesStack(NodesStack* nodesStack, ModuleNode* moduleNode) {
  if (nodesStack->capacity < nodesStack->count + 1) {
    int oldCapacity = nodesStack->capacity;
    nodesStack->capacity = GROW_CAPACITY(nodesStack->capacity);
    nodesStack->list = GROW_ARRAY(ModuleNode*, nodesStack->list, oldCapacity,
                                  nodesStack->capacity);
  }

  nodesStack->list[nodesStack->count++] = moduleNode;
}

static ModuleNode* popNodesStack(NodesStack* nodesStack) {
  return nodesStack->list[--nodesStack->count];
}

static void freeNodesStack(NodesStack* nodesStack) {
  FREE_ARRAY(ModuleNode*, nodesStack->list, nodesStack->capacity);
}

static void initSeenNodes(SeenNodes* seenNodes) {
  seenNodes->count = 0;
  seenNodes->capacity = 0;
  seenNodes->list = NULL;
}

static void insertSeenNodes(SeenNodes* seenNodes, ModuleNode* moduleNode) {
  if (seenNodes->capacity < seenNodes->count + 1) {
    int oldCapacity = seenNodes->capacity;
    seenNodes->capacity = GROW_CAPACITY(seenNodes->capacity);
    seenNodes->list = GROW_ARRAY(ModuleNode*, seenNodes->list, oldCapacity,
                                 seenNodes->capacity);
  }

  seenNodes->list[seenNodes->count++] = moduleNode;
}

static bool hasSeenNode(SeenNodes* seenNodes, ModuleNode* moduleNode) {
  for (int idx = 0; idx < seenNodes->count; idx++) {
    if (seenNodes->list[idx]->id == moduleNode->id) {
      return true;
    }
  }

  return false;
}

static void freeSeenNodes(SeenNodes* seenNodes) {
  FREE_ARRAY(ModuleNode*, seenNodes->list, seenNodes->capacity);
}

static ModuleNode* allocateNode(module_id_t id) {
  ModuleNode* node = malloc(sizeof(ModuleNode));

  node->state = COMPILING_STATE;
  node->id = id;
  node->imports = NULL;
  node->importsCount = 0;
  node->importsCapacity = 0;
  node->module = NULL;

  return node;
}

void initModules(Modules* modules, const char* absPath) {
  modules->root = allocateNode(hashString(absPath, strlen(absPath)));
}

static bool searchNode(Modules* modules, module_id_t moduleId,
                       ModuleNode** responseNode) {
  NodesStack nodesStack;
  SeenNodes seenNodes;

  initNodesStack(&nodesStack);
  initSeenNodes(&seenNodes);

  insertSeenNodes(&seenNodes, modules->root);
  pushNodesStack(&nodesStack, modules->root);

  while (nodesStack.count > 0) {
    ModuleNode* node = popNodesStack(&nodesStack);

    if (node->id == moduleId) {
      *responseNode = node;
      freeNodesStack(&nodesStack);
      freeSeenNodes(&seenNodes);

      return true;
    }

    for (int idx = 0; idx < node->importsCount; idx++) {
      if (!hasSeenNode(&seenNodes, node->imports[idx])) {
        insertSeenNodes(&seenNodes, node->imports[idx]);
        pushNodesStack(&nodesStack, node->imports[idx]);
      }
    }
  }

  freeNodesStack(&nodesStack);
  freeSeenNodes(&seenNodes);
  return false;
}

bool findModuleNode(Modules* modules, ModuleNode* origin, ModuleNode** node,
                    const char* absPath) {
  module_id_t targetId = hashString(absPath, strlen(absPath));
  ModuleNode* target = NULL;

  // module not found, you have to create a module
  if (!searchNode(modules, targetId, &target)) {
    return false;
  }
  // import a module that is still compiling seems to be equivalent to cyclic
  // dependency
  if (target->state == COMPILING_STATE) {
    fprintf(stderr, "Unexpected modules cyclic dependency.\n");
    exit(1);
  }

  *node = target;

  return true;
}

void createModuleNode(ModuleNode** node, const char* absPath) {
  *node = allocateNode(hashString(absPath, strlen(absPath)));
}

void createDependency(ModuleNode* origin, ModuleNode* target) {
  if (origin->importsCapacity < origin->importsCount + 1) {
    int oldCapacity = origin->importsCapacity;
    origin->importsCapacity = GROW_CAPACITY(oldCapacity);
    origin->imports = GROW_ARRAY(ModuleNode*, origin->imports, oldCapacity,
                                 origin->importsCapacity);
  }

  origin->imports[origin->importsCount++] = target;
}

void resolveModuleNode(ModuleNode* node, ObjModule* module) {
  node->module = module;
  node->state = COMPILED_STATE;
}

static void freeNode(ModuleNode* node) {
  FREE_ARRAY(ModuleNode*, node->imports, node->importsCapacity);
  free(node);
}

void freeModules(Modules* modules) {
  NodesStack nodesStack;
  SeenNodes seenNodes;

  initNodesStack(&nodesStack);
  initSeenNodes(&seenNodes);

  insertSeenNodes(&seenNodes, modules->root);
  pushNodesStack(&nodesStack, modules->root);

  while (nodesStack.count > 0) {
    ModuleNode* node = popNodesStack(&nodesStack);

    for (int idx = 0; idx < node->importsCount; idx++) {
      if (!hasSeenNode(&seenNodes, node->imports[idx])) {
        insertSeenNodes(&seenNodes, node->imports[idx]);
        pushNodesStack(&nodesStack, node->imports[idx]);
      }
    }
  }

  for (int idx = 0; idx < seenNodes.count; idx++) {
    freeNode(seenNodes.list[idx]);
  }

  freeNodesStack(&nodesStack);
  freeSeenNodes(&seenNodes);
}