
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "modules.h"
#include "memory.h"
#include "utils.h"

// todo: implement hashset for O(1) lookup perfomance
typedef struct {
    int count;
    int capacity;
    ModuleNode** list; 
} SeenNodes;

typedef struct {
    int count;
    int capacity;
    ModuleNode** list; 
} NodesStack;

static void initNodesStack(NodesStack* nodesStack) {
    nodesStack->count = 0;
    nodesStack->capacity = 0;
    nodesStack->list = NULL;
}

static void pushNodesStack(NodesStack* nodesStack, ModuleNode* moduleNode) {
    if (nodesStack->capacity < nodesStack->count + 1) {
        int oldCapacity = nodesStack->capacity;
        nodesStack->capacity = GROW_CAPACITY(nodesStack->capacity);
        nodesStack->list = GROW_ARRAY(ModuleNode*, nodesStack->list, oldCapacity, nodesStack->capacity);
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
        seenNodes->list = GROW_ARRAY(ModuleNode*, seenNodes->list, oldCapacity, seenNodes->capacity);
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

void initModules(Modules* modules, const char* source) {
    modules->root = allocateNode(hashString(source, strlen(source)));
}

static bool searchNode(Modules* modules, module_id_t moduleId, ModuleNode** responseNode) {
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

bool addDependency(Modules* modules, ModuleNode* origin, ModuleNode** node, const char* absPath) {
    module_id_t targetId = hashString(absPath, strlen(absPath));
    ModuleNode *target = NULL;

    // module not found, you have to create a module 
    if (!searchNode(modules, targetId, &target)) {
        return false;
    } 
    // import a module that is still compiling seems to be equivalent to cyclic dependency
    if (target->state == COMPILING_STATE) {
        fprintf(stderr,"Unexpected modules cyclic dependency.\n");
        exit(1);
    }
    
    *node = target;

    if (origin->importsCapacity < origin->importsCount + 1) {
        int oldCapacity = origin->importsCapacity;
        origin->importsCapacity = GROW_CAPACITY(oldCapacity);
        origin->imports = GROW_ARRAY(ModuleNode*, origin->imports, oldCapacity, origin->importsCapacity);
    }

    origin->imports[origin->importsCount++] = target;

    return true;
}

void createModuleNode(ModuleNode* origin, ModuleNode** moduleNode, const char* absPath, const char* source) {
    ModuleNode* node = allocateNode(hashString(absPath, strlen(absPath)));

    if (origin->importsCapacity < origin->importsCount + 1) {
        int oldCapacity = origin->importsCapacity;
        origin->importsCapacity = GROW_CAPACITY(oldCapacity);
        origin->imports = GROW_ARRAY(ModuleNode*, origin->imports, oldCapacity, origin->importsCapacity);
    }

    origin->imports[origin->importsCount++] = node;

    *moduleNode = node;
}

void resolveDependency(ModuleNode* node, ObjModule* module) {
    node->module = module;
    node->state = COMPILED_STATE;
}

static void freeNode(ModuleNode* node) {
    FREE_ARRAY(ModuleNode*, node->imports, node->importsCapacity);
    free(node);
}

void freeModules(Modules *modules) {
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