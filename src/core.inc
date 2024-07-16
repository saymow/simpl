class Array {
    map(callback) {
        var length = this.length();
        var newArray = Array.new(length);

        for (var idx = 0; idx < length; idx += 1)  {
            newArray[idx] = callback(this[idx]); 
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
            if (callback(this[idx])) {
                newArray.push(this[idx]); 
            }
        }

        return newArray;
    }
}