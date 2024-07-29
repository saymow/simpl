// This file is generated from core.inc 

#ifndef CORE_EXT
#define CORE_EXT

char* coreExtension =
    "class Error {}\n"
    "\n"
    "class String {\n"
    "  trim() {\n"
    "    return this.trimStart().trimEnd();\n"
    "  }\n"
    "}\n"
    "\n"
    "class Array {\n"
    "  map(callback) {\n"
    "    var length = this.length();\n"
    "    var newArray = Array.new(length);\n"
    "\n"
    "    for (var idx = 0; idx < length; idx += 1)  {\n"
    "      newArray[idx] = callback(this[idx], idx, this);\n"
    "    }\n"
    "\n"
    "    return newArray;\n"
    "  }\n"
    "\n"
    "  // Should we allocate the newArray with the base array length?\n"
    "  // there might be a good heuristic for this decision.\n"
    "  // But in order to do that, we would need access to a native method\n"
    "  // that just upfrontly allocate the memory without changing the length.\n"
    "  filter(callback) {\n"
    "    var newArray = Array.new();\n"
    "    var length = this.length();\n"
    "\n"
    "    for (var idx = 0; idx < length; idx += 1)  {\n"
    "      if (callback(this[idx], idx, this)) {\n"
    "        newArray.push(this[idx]);\n"
    "      }\n"
    "    }\n"
    "\n"
    "    return newArray;\n"
    "  }\n"
    "\n"
    "  find(callback) {\n"
    "    var length = this.length();\n"
    "\n"
    "    for (var idx = 0; idx < length; idx += 1)  {\n"
    "      if (callback(this[idx], idx, this)) {\n"
    "        return this[idx];\n"
    "      }\n"
    "    }\n"
    "\n"
    "    return nil;\n"
    "  }\n"
    "\n"
    "  reduce(callback) {\n"
    "    var length = this.length();\n"
    "\n"
    "    if (length == 0) {\n"
    "      return nil;\n"
    "    }\n"
    "\n"
    "    var acc = this[0];\n"
    "\n"
    "    for (var idx = 1; idx < length; idx += 1) {\n"
    "      acc = callback(acc, this[idx], idx, this);\n"
    "    }\n"
    "\n"
    "    return acc;\n"
    "  }\n"
    "\n"
    "  reduce(callback, acc) {\n"
    "    var length = this.length();\n"
    "\n"
    "    for (var idx = 0; idx < length; idx += 1) {\n"
    "      acc = callback(acc, this[idx], idx, this);\n"
    "    }\n"
    "\n"
    "    return acc;\n"
    "  }\n"
    "}";

#endif