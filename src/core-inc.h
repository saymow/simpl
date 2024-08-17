// This file is generated from core.inc 

#ifndef CORE_EXT
#define CORE_EXT

char* coreExtension =
	"class Math {}\n"
	"\n"
	"class Error {\n"
	"  toString() {\n"
	"    return this.message + \"\n\" + this.stack;\n"
	"  }\n"
	"}\n"
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
	"    for idx in range(length)  {\n"
	"      newArray[idx] = callback(this[idx], idx, this); \n"
	"    }\n"
	"\n"
	"    return newArray;\n"
	"  }\n"
	"\n"
	"  // Should we allocate the newArray with the base array length?\n"
	"  // there might be a good heuristic for this decision.\n"
	"  // But in order to do that, we would need access to a native method\n"
	"  // that just upfrontly allocate the memory without changing the length.  \n"
	"  filter(callback) {\n"
	"    var length = this.length();                \n"
	"    var newArray = Array.new();\n"
	"\n"
	"    for idx in range(length) {\n"
	"      if (callback(this[idx], idx, this)) {\n"
	"        newArray.push(this[idx]); \n"
	"      }\n"
	"    }\n"
	"\n"
	"    return newArray;\n"
	"  }\n"
	"\n"
	"  find(callback) {\n"
	"    for idx in range(this.length())  {\n"
	"      if (callback(this[idx], idx, this)) {\n"
	"        return this[idx];\n"
	"      }\n"
	"    }\n"
	"\n"
	"    return nil;\n"
	"  }\n"
	"\n"
	"  findIndex(callback) {\n"
	"    for idx in range(this.length())  {\n"
	"      if (callback(this[idx], idx, this)) {\n"
	"        return idx;\n"
	"      }\n"
	"    }\n"
	"\n"
	"    return -1;\n"
	"  }\n"
	"\n"
	"  reduce(callback) {\n"
	"    var length = this.length();\n"
	"    \n"
	"    if (length == 0) {\n"
	"      return nil;\n"
	"    }\n"
	"\n"
	"    var acc = this[0];\n"
	"\n"
	"    for idx in range(1, length) {\n"
	"      acc = callback(acc, this[idx], idx, this);\n"
	"    }\n"
	"\n"
	"    return acc;     \n"
	"  }\n"
	"\n"
	"  reduce(callback, acc) {\n"
	"    for idx in range(this.length()) {\n"
	"      acc = callback(acc, this[idx], idx, this);\n"
	"    }\n"
	"\n"
	"    return acc;     \n"
	"  }\n"
	"\n"
	"  sort() {\n"
	"    return this.sort((a, b) -> a < b);\n"
	"  }\n"
	"\n"
	"  sort(compare) {\n"
	"    fun partition(left, right) {\n"
	"      var pivot = (left + right) / 2;\n"
	"      var count = left;\n"
	"\n"
	"      var tmp = this[right]; \n"
	"      this[right] = this[pivot];\n"
	"      this[pivot] = tmp;\n"
	"\n"
	"      for idx in range(left, right) {\n"
	"        if (compare(this[idx], this[right])) {\n"
	"          tmp = this[idx];\n"
	"          this[idx] = this[count];\n"
	"          this[count] = tmp;\n"
	"          count += 1;\n"
	"        }\n"
	"      }\n"
	"\n"
	"      tmp = this[right];\n"
	"      this[right] = this[count];\n"
	"      this[count] = tmp; \n"
	"\n"
	"      return count;\n"
	"    }\n"
	"    \n"
	"    fun quickSort(left, right) {\n"
	"      if (left >= right) return;\n"
	"      var pi = partition(left, right);\n"
	"      quickSort(left, pi - 1);\n"
	"      quickSort(pi + 1, right);\n"
	"    } \n"
	"\n"
	"    quickSort(0, this.length() - 1);\n"
	"    \n"
	"    return this;   \n"
	"  }\n"
	"\n"
	"  skip(count) {\n"
	"    return this.slice(Math.max(count, 0));\n"
	"  }\n"
	"}\n";

#endif
