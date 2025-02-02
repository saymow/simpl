class BinaryTree {
    BinaryTree() {
        this.root = nil;
        this.size = 0;
    }

    __compare(a, b) {
        if (Number.isNumber(a) and Number.isNumber(b)) {
            return a - b;
        } else if (String.isString(a) and String.isString(b)) {
            return a.hash() - b.hash();
        }

        throw Error("Invalid data type.");
    }

    __height(node) {
        if (node == nil) {
            return 0;
        }

        return node.height;
    }

    __balance(node) {
        if (node == nil) {
            return 0;
        }

        return this.__height(node.right) - this.__height(node.left);
    }

    __rotateRight(node) {
        var tmp = node.left;
        var tmp2 = node.left.right;
        node.left.right = node;
        node.left = tmp2;

        node.height = Math.max(this.__height(node.left), this.__height(node.right)) + 1;
        tmp.height = Math.max(this.__height(tmp.left), this.__height(tmp.right)) + 1;
    
        return tmp;
    }

    __rotateLeft(node) {
        var tmp = node.right;
        var tmp2 = node.right.left;
        node.right.left = node;
        node.right = tmp2;

        node.height = Math.max(this.__height(node.left), this.__height(node.right)) + 1;
        tmp.height = Math.max(this.__height(tmp.left), this.__height(tmp.right)) + 1;

        return tmp;
    }

    __insert(root, key, value) {
        if (root == nil) {
            return { key, value, height: 1, left: nil, right: nil };
        }

        var comparison = this.__compare(root.key, key);

        if (comparison > 0) {
            root.left = this.__insert(root.left, key, value); 
        } else if (comparison < 0) {
            root.right = this.__insert(root.right, key, value);
        }

        root.height = Math.max(this.__height(root.left), this.__height(root.right)) + 1;
        var rootBalance = this.__balance(root);

        if (rootBalance < -1 and this.__balance(root.left) <= 0) {
            // left-left case
        
            return this.__rotateRight(root);
        }
        if (rootBalance < -1 and this.__balance(root.left) >= 0) {
            // left-right case
            
            root.left = this.__rotateLeft(root.left);
            return this.__rotateRight(root);
        }
        if (rootBalance > 1 and this.__balance(root.right) >= 0) {
            // right-right case

            return this.__rotateLeft(root);
        }
        if (rootBalance > 1 and this.__balance(root.right) <= 0) {
            // right-left case
            
            root.right = this.__rotateRight(root.right);
            return this.__rotateLeft(root);
        }

        return root;
    }

    insert(key, value) {
        this.root = this.__insert(this.root, key, value);
        this.size += 1;
    }

    __find(root, key) {
        if (root == nil) {
            return nil;
        }

        var cmp = this.__compare(root.key, key);

        if (cmp < 0) {
            return this.__find(root.right, key);
        } else if (cmp > 0) {
            return this.__find(root.left, key);
        }

        return root;
    }

    find(key) {
        var node = this.__find(this.root, key);

        if (node == nil) {
            return nil;
        }
        
        return node.value;
    }
}