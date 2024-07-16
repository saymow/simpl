// This file is generated from core.inc 

#ifndef CORE_EXT
#define CORE_EXT

const char* coreExtension = 
"class Array {\n"
"    map(callback) {\n"
"        var length = this.length();\n"
"        var newArray = Array.new(length);\n"
"\n"
"        for (var idx = 0; idx < length; idx += 1) {\n"
"            newArray[idx] = callback(this[idx]);\n"
"        }\n"
"\n"
"        return newArray;\n"
"    }\n"
"}";

#endif