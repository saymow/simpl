class Math {}

class Error {
  toString() {
    return this.message + "\n" + this.stack;
  }
}

class String {
  trim() {
    return this.trimStart().trimEnd();
  }
}

class Array {
  map(callback) {
    var length = this.length();
    var newArray = Array.new(length);

    for idx in range(length)  {
      newArray[idx] = callback(this[idx], idx, this); 
    }

    return newArray;
  }

  // Should we allocate the newArray with the base array length?
  // there might be a good heuristic for this decision.
  // But in order to do that, we would need access to a native method
  // that just upfrontly allocate the memory without changing the length.  
  filter(callback) {
    var length = this.length();                
    var newArray = Array.new();

    for idx in range(length) {
      if (callback(this[idx], idx, this)) {
        newArray.push(this[idx]); 
      }
    }

    return newArray;
  }

  find(callback) {
    for idx in range(this.length())  {
      if (callback(this[idx], idx, this)) {
        return this[idx];
      }
    }

    return nil;
  }

  findIndex(callback) {
    for idx in range(this.length())  {
      if (callback(this[idx], idx, this)) {
        return idx;
      }
    }

    return -1;
  }

  reduce(callback) {
    var length = this.length();
    
    if (length == 0) {
      return nil;
    }

    var acc = this[0];

    for idx in range(1, length) {
      acc = callback(acc, this[idx], idx, this);
    }

    return acc;     
  }

  reduce(callback, acc) {
    for idx in range(this.length()) {
      acc = callback(acc, this[idx], idx, this);
    }

    return acc;     
  }

  sort() {
    return this.sort((a, b) -> a < b);
  }

  sort(compare) {
    fun partition(left, right) {
      var pivot = (left + right) / 2;
      var count = left;

      var tmp = this[right]; 
      this[right] = this[pivot];
      this[pivot] = tmp;

      for idx in range(left, right) {
        if (compare(this[idx], this[right])) {
          tmp = this[idx];
          this[idx] = this[count];
          this[count] = tmp;
          count += 1;
        }
      }

      tmp = this[right];
      this[right] = this[count];
      this[count] = tmp; 

      return count;
    }
    
    fun quickSort(left, right) {
      if (left >= right) return;
      var pi = partition(left, right);
      quickSort(left, pi - 1);
      quickSort(pi + 1, right);
    } 

    quickSort(0, this.length() - 1);
    
    return this;   
  }

  skip(count) {
    return this.slice(Math.max(count, 0));
  }
}