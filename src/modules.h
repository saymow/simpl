#ifndef modules_h
#define modules_h

#include "common.h"
#include "object.h"

typedef uint32_t module_id_t;

typedef enum { COMPILED_STATE, COMPILING_STATE } ModuleState;

typedef struct ModuleNode {
    uint32_t id;
    ModuleState state;
    ObjModule* module; 
    struct ModuleNode** imports;
    int importsCount;
    int importsCapacity;
} ModuleNode; 

typedef struct {
    ModuleNode* root; 
} Modules;

void initModules(Modules* modules, const char* source);
bool addDependency(Modules* modules, ModuleNode* origin, ModuleNode** node, const char* absPath); 
void createModuleNode(ModuleNode* origin, ModuleNode** node, const char* absPath, const char* source);
void resolveDependency(ModuleNode* node, ObjModule* module);
void freeModules(Modules *modules);

#endif