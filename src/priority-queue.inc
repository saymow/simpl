class PriorityQueue {
    PriorityQueue(keyExtractor) {
        this.arr = [];
        this.keyExtractor = keyExtractor;  
    }

    __swap(i, j) {
        var tmp = this.arr[i];
        this.arr[i] = this.arr[j];
        this.arr[j] = tmp;
    }

    __bubbleUp(idx) {
        if (idx == 0) {
            return;
        }

        var parentIdx = Number.toInteger((idx - 1) / 2);
    
        if (this.keyExtractor(this.arr[idx]) < this.keyExtractor(this.arr[parentIdx])) {
            this.__swap(idx, parentIdx);
            this.__bubbleUp(parentIdx);
        }
    }

    enqueue(item) {
        this.__bubbleUp(this.arr.push(item) - 1);
    }

    __heapify(idx) {
        var leftIdx = 2 * idx + 1;
        var rightIdx = 2 * idx + 2;
        var lowestValueIdx = idx;

        if (leftIdx < this.size() and this.keyExtractor(this.arr[leftIdx]) < this.keyExtractor(this.arr[lowestValueIdx])) {
            lowestValueIdx = leftIdx;
        }

        if (rightIdx < this.size() and this.keyExtractor(this.arr[rightIdx]) < this.keyExtractor(this.arr[lowestValueIdx])) {
            lowestValueIdx = rightIdx;
        }

        if (lowestValueIdx != idx) {
            this.__swap(lowestValueIdx, idx);
            this.__heapify(lowestValueIdx);
        }
    }

    dequeue() {
        if (this.size() == 0) {
            return nil;
        }

        var item = this.arr[0];
        this.arr[0] = this.arr[this.size() - 1]; 
        this.arr.pop();
        this.__heapify(0);

        return item;
    }

    peek() {
        if (this.size() == 0) {
            return nil;
        }

        return this.arr[0];
    }

    size() {
        return this.arr.length();
    }
}