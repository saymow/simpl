#ifndef modules_h
#define modules_h

#include "common.h"
#include "object.h"

// Module id is a hash of the module absolute path and shoud be of type uint32_t
typedef uint32_t module_id_t;

// Module is created in COMPILING_STATE and after compilation turns to
// COMPILED_STATE
typedef enum { COMPILING_STATE, COMPILED_STATE } ModuleState;

// Module node structure
typedef struct ModuleNode {
  // Module id
  uint32_t id;

  // Module state
  ModuleState state;

  // Runtime module structure
  ObjModule* module;

  // Module imports registry
  struct ModuleNode** imports;
  int importsCount;
  int importsCapacity;
} ModuleNode;

// Module graph root node holder
typedef struct {
  ModuleNode* root;
} Modules;

// Initialize Modules structure with root node
void initModules(Modules* modules, const char* absPath);

// Perform a search on the graph for a node with absPath
bool findModuleNode(Modules* modules, ModuleNode* origin, ModuleNode** node,
                    const char* absPath);

// Create a new module node for absPath
void createModuleNode(ModuleNode** node, const char* absPath);

// Create a graph edge between origin and target nodes
void createDependency(ModuleNode* origin, ModuleNode* target);

// Resolve node to COMPILED_STATE and set runtime module
void resolveModuleNode(ModuleNode* node, ObjModule* module);

// Free modules graph
void freeModules(Modules* modules);

#endif