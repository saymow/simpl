class Array {
    map(callback) {
        var length = this.length();
        var newArray = Array.new(length);

        for (var idx = 0; idx < length; idx += 1)  {
            newArray[idx] = callback(this[idx]); 
        }

        return newArray;
    }
}