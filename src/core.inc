class String {
  String(value) {
    this = value.toString();
  }
}

class Array {
  Array() {
    this = Array.new();
  }
  
  Array(length) {
    this = Array.new(length);
  }

  map(callback) {
    var length = this.length();
    var newArray = Array.new(length);

    for (var idx = 0; idx < length; idx += 1)  {
      newArray[idx] = callback(this[idx], idx, this); 
    }

    return newArray;
  }

  // Should we allocate the newArray with the base array length?
  // there might be a good heuristic for this decision.
  // But in order to do that, we would need access to a native method
  // that just upfrontly allocate the memory without changing the length.  
  filter(callback) {
    var newArray = Array.new();
    var length = this.length();                

    for (var idx = 0; idx < length; idx += 1)  {
      if (callback(this[idx], idx, this)) {
        newArray.push(this[idx]); 
      }
    }

    return newArray;
  }

  find(callback) {
    var length = this.length();                

    for (var idx = 0; idx < length; idx += 1)  {
      if (callback(this[idx], idx, this)) {
        return this[idx];
      }
    }

    return nil;
  }

  reduce(callback) {
    var length = this.length();
    
    if (length == 0) {
      return nil;
    }

    var acc = this[0];

    for (var idx = 1; idx < length; idx += 1) {
      acc = callback(acc, this[idx], idx, this);
    }

    return acc;     
  }

  reduce(callback, acc) {
    var length = this.length();
    
    for (var idx = 0; idx < length; idx += 1) {
      acc = callback(acc, this[idx], idx, this);
    }

    return acc;     
  }
}