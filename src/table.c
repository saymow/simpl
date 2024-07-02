#include "table.h"

#include "memory.h"
#include "string.h"

#define TABLE_MAX_LOAD .75

void initTable(Table* table) {
  table->count = 0;
  table->capacity = 0;
  table->entries = NULL;
}

void freeTable(Table* table) {
  FREE_ARRAY(Entry, table->entries, table->capacity);
  initTable(table);
}

static Entry* findEntry(Entry* entries, int capacity, ObjString* key) {
  uint32_t idx = key->hash % capacity;

  for (;;) {
    Entry* entry = &entries[idx];
    Entry* tombstone = NULL;

    if (entry->key == NULL) {
      if (IS_NIL(entry->value)) {
        // Empty entry
        return tombstone == NULL ? entry : tombstone;
      } else {
        // Tombstone found
        if (tombstone == NULL) {
          tombstone = entry;
        }
      }
    } else if (entry->key == key) {
      return entry;
    }

    idx = (idx + 1) % capacity;
  }
}

bool tableGet(Table* table, ObjString* key, Value* value) {
  if (table->count == 0) return false;

  Entry* entry = findEntry(table->entries, table->capacity, key);

  if (entry->key == NULL) {
    return false;
  }

  *value = entry->value;
  return true;
}

static void adjustCapacity(Table* table, int capacity) {
  Entry* entries = ALLOCATE(Entry, capacity);

  for (int idx = 0; idx < capacity; idx++) {
    entries[idx].key = NULL;
    entries[idx].value = NIL_VAL;
  }

  table->count = 0;
  for (int idx = 0; idx < table->capacity; idx++) {
    Entry* entry = &table->entries[idx];

    if (entry->key == NULL) continue;

    table->count++;
    Entry* dest = findEntry(entries, capacity, entry->key);
    dest->key = entry->key;
    dest->value = entry->value;
  }

  FREE_ARRAY(Entry, table->entries, table->capacity);
  table->entries = entries;
  table->capacity = capacity;
}

bool tableSet(Table* table, ObjString* key, Value value) {
  if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
    adjustCapacity(table, GROW_CAPACITY(table->capacity));
  }

  Entry* entry = findEntry(table->entries, table->capacity, key);
  bool isNewKey = entry->key == NULL;

  // is a new key and entry is not a tombstone => table->count++
  if (isNewKey && IS_NIL(entry->value)) table->count++;

  entry->key = key;
  entry->value = value;
  return isNewKey;
}

bool tableDelete(Table* table, ObjString* key) {
  if (table->count == 0) return false;

  Entry* entry = findEntry(table->entries, table->capacity, key);

  if (entry->key == NULL) return false;

  // setting tombstone
  entry->key = NULL;
  entry->value = BOOL_VAL(true);

  return true;
}

void tableAddAll(Table* from, Table* to) {
  for (int idx = 0; idx < from->capacity; idx++) {
    Entry* entry = &from->entries[idx];
    if (entry->key != NULL) {
      tableSet(to, entry->key, entry->value);
    }
  }
}

ObjString* tableFindString(Table* table, const char* chars, int length,
                           uint32_t hash) {
  if (table->count == 0) return NULL;

  uint32_t idx = hash % table->capacity;

  for (;;) {
    Entry* entry = &table->entries[idx];

    if (entry->key == NULL) {
      if (IS_NIL(entry->value)) {
        return NULL;
      }
    } else if (entry->key->length == length && entry->key->hash == hash &&
               memcmp(entry->key->chars, chars, length) == 0) {
      return entry->key;
    }

    idx = (idx + 1) % table->capacity;
  }
}

void markTable(Table* table) {
  for (int idx = 0; idx < table->capacity; idx++) {
    Entry* entry = &table->entries[idx];
    markObject((Obj*)entry->key);
    markValue(entry->value);
  }
}

void tableRemoveNotReferenced(Table* table) {
  for (int idx = 0; idx < table->capacity; idx++) {
    Entry* entry = &table->entries[idx];
    if (entry->key != NULL && !entry->key->obj.isMarked) {
      tableDelete(table, entry->key);
    }
  }
}
