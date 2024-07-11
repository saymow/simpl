import fs from "fs";
import util from "util";
import { BenchmarkResults } from "./benchmark-test-suite-runner";
import { TestFile } from "./files-reader";

const appendFile = util.promisify(fs.appendFile);

class BenchmarkTestSaver {
  constructor(
    private readonly title: string,
    private readonly testFile: TestFile,
    private readonly benchmark: BenchmarkResults
  ) {}

  async execute() {
    let str = "\n";

    str += "// -------------------------- BENCHMARK ---------------------------\n";
    str += `// ${"title".padEnd(32)}${this.title.padStart(34)}`;
    str += `// ${"rounds".padEnd(32)}${this.benchmark.rounds.toString().padStart(32)}\n`;
    str += `// ${"min".padEnd(32)}${this.benchmark.min.padStart(32)}\n`;
    str += `// ${"max".padEnd(32)}${this.benchmark.max.padStart(32)}\n`;
    str += `// ${"median".padEnd(32)}${this.benchmark.median.padStart(32)}\n`;
    str += `// ${"avg".padEnd(32)}${this.benchmark.avg.padStart(32)}\n`;
    str += `// ${"stdDeviation".padEnd(32)}${this.benchmark.stdDeviation.padStart(32)}`;

    return appendFile(this.testFile.path, str);
  }
}

export default BenchmarkTestSaver;
