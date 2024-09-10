<h1 align="center">Simpl</h1>

<br>

<p>&nbsp;&nbsp;&nbsp;&nbsp; <b>Simpl</b> is a simple <a href="https://en.wikipedia.org/wiki/Object-oriented_programming" target="_blank">OOP</a> <a href="https://en.wikipedia.org/wiki/Parallel_computing" target="_blank">parallel</a> <a href="https://dev.to/lexplt/understanding-bytecode-interpreters-eig" target="_blank">bytecode script language</a>. <b>Simpl</b> has a cousing programming language that runs on the Web, you can look at <a href="https://github.com/saymow/simpl-web" target="_blank">Simpl Web</a>.</p>

<p><b>Simpl</b> has a <a href="https://github.com/saymow/simpl/tree/master/tests-runner/src" target="_blank">test runner</a>, hundreds of <a href="https://github.com/saymow/simpl/blob/master/tests/language/ternary-operator.simpl" target="_blank">test files</a> and few scripts to run <a href="https://github.com/saymow/simpl/blob/master/tests/benchmark/lexer.simpl" target="_blank">benchmarks</a>.</p>

<p>You can get a feel for <b>Simpl</b> syntax by looking at this <a href="https://reference.wolfram.com/language/ref/ParallelSum.html" target="_blank">parallel sum</a> implementation: </p>

```
fun program(ctx) {
    var sum = 0;

    for idx in range(ctx.start, ctx.end) {
        sum += ctx.array[idx];
    }

    return sum;
}

fun papallelSum(array, threadsCount) {
    var threads = Array(threadsCount);
    var count = 0;

    for idx in range(threadsCount) {
        var start = count;
        count += array.length() / threadsCount;
        threads[idx] = System.Threading.start(program, { array: array, start: start, end: count });
    } 

    return threads.reduce((acc, thread) -> acc + System.Threading.join(thread), 0);
}

var array = Array(100000).map(() -> 1);

System.log(papallelSum(array, 5));
```
