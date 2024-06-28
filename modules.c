
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "modules.h"
#include "memory.h"
#include "utils.h"

static ModuleNode* allocateNode(module_id_t id) {
    ModuleNode* node = malloc(sizeof(ModuleNode));

    node->state = COMPILING_STATE;
    node->id = id;
    node->imports = NULL;
    node->importsCount = 0;    
    node->importsCapacity = 0;

    return node;
}

void initModules(Modules* modules, const char* source) {
    modules->root = allocateNode(hashString(source, strlen(source)));
}

static bool searchNode(ModuleNode* origin, module_id_t moduleId, ModuleNode** node) {
    if (origin->id == moduleId) {
        *node = origin; 
        return true;
    }

    for (int idx = 0; idx < origin->importsCount; idx++) {
        if (searchNode(origin->imports[idx], moduleId, node)) {
            return true;
        }
    }

    return false;   
}

bool addDependency(Modules* modules, ModuleNode* origin, ModuleNode** module, const char* source) {
    module_id_t targetId = hashString(source, strlen(source));
    ModuleNode *target = NULL;

    // module not found, i.e, you have to create a module 
    if (!searchNode(modules->root, targetId, &target)) {
        return false;
    } 
    // import a module that is still compiling seems to be equivalent to cyclic dependency
    if (target->state == COMPILING_STATE) {
        printf("Unexpected modules cyclic dependency.\n");
        exit(1);
    }

    *module = target;
    return true;
}

void createModule(Modules* modules, ModuleNode* origin, ModuleNode** module, const char* source) {
    module_id_t targetId = hashString(source, strlen(source));
    ModuleNode* node = allocateNode(targetId);

    if (origin->importsCapacity < origin->importsCount + 1) {
        int oldCapacity = origin->importsCapacity;
        origin->importsCapacity = GROW_CAPACITY(oldCapacity);
        origin->imports = GROW_ARRAY(ModuleNode*, origin->imports, oldCapacity, origin->importsCapacity);
    }

    origin->imports[origin->importsCount++] = node;

    *module = node;
}

void resolveDependency(Modules* modules, ModuleNode* node, ObjFunction* chunk) {
    node->chunk = chunk;
    node->state = COMPILED_STATE;
}

static void freeNode(ModuleNode* node) {
    for (int idx = 0; idx < node->importsCount; idx++) {
        freeNode(node->imports[idx]);
    }

    FREE_ARRAY(ModuleNode*, node->imports, node->importsCount);
    free(node);
}

void freeModules(Modules *modules) {
    freeNode(modules->root);
}