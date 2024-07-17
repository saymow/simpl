// This file is generated from core.inc 

#ifndef CORE_EXT
#define CORE_EXT

char* coreExtension = 
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
"}";

#endif