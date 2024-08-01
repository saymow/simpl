import BenchmarkTestSaver from "./benchmark-test-saver";
import BenchmarkTestSuiteRunner, {
  BenchmarkResults,
} from "./benchmark-test-suite-runner";
import FilesReader, { TestFile } from "./files-reader";
import { Settings, TestRunnerController } from "./interfaces";
import TestReader, { TestSuite } from "./test-reader";
import colors from "colors";
import { sleep } from "./utils";

enum TestCommand {
  Repeat,
  Return,
  Save,
}

class BenchmarkTestRunnerController extends TestRunnerController {
  async readLine(): Promise<string> {
    return new Promise<string>((resolve) => {
      process.stdin.setRawMode(false);

      const handleEvent = (data: Buffer) => {
        process.removeListener("data", handleEvent);
        process.stdin.setRawMode(true);
        resolve(data.toString());
      };

      process.stdin.on("data", handleEvent);
    });
  }

  async listenUntilCommand(): Promise<TestCommand> {
    return new Promise<TestCommand>((resolve) => {
      const handleEvent = (_: any, key: any) => {
        if (key.name === "r") {
          process.stdin.removeListener("keypress", handleEvent);
          resolve(TestCommand.Repeat);
        } else if (key.name === "n") {
          process.stdin.removeListener("keypress", handleEvent);
          resolve(TestCommand.Return);
        } else if (key.name === "s") {
          process.stdin.removeListener("keypress", handleEvent);
          resolve(TestCommand.Save);
        } else if (key.name === "c" && key.ctrl) {
          process.exit(0);
        }
      };

      process.stdin.on("keypress", handleEvent);
    });
  }

  async listenUntilBenchmarkSelected(
    testSuites: TestSuite[]
  ): Promise<TestSuite | null> {
    return new Promise<TestSuite | null>((resolve) => {
      const handleEvent = (_: any, key: any) => {
        const testIndex = parseInt(key.name);

        if (testIndex >= 0 && testIndex < testSuites.length) {
          process.stdin.removeListener("keypress", handleEvent);
          resolve(testSuites[testIndex]);
        } else if (testIndex == testSuites.length) {
          process.stdin.removeListener("keypress", handleEvent);
          resolve(null);
        } else if (key.name === "c" && key.ctrl) {
          process.exit(0);
        }
      };

      process.stdin.on("keypress", handleEvent);
    });
  }

  async executeBenchmark(benchmark: TestSuite) {
    const testSuiteRunner = new BenchmarkTestSuiteRunner(
      this.vmPath,
      benchmark
    );

    commandsLoop: while (true) {
      const benchmarkResult = await testSuiteRunner.execute();

      this.displayCommands();

      const command = await this.listenUntilCommand();

      switch (command) {
        case TestCommand.Repeat: {
          console.clear();
          break;
        }
        case TestCommand.Return: {
          console.clear();
          break commandsLoop;
        }
        case TestCommand.Save: {
          console.clear();
          console.log("Benchmark title: ");

          const benchmarkTitle = await this.readLine();
          const benchmarkSaver = new BenchmarkTestSaver(
            benchmarkTitle,
            benchmark.testFile,
            benchmarkResult
          );

          console.log("Saving...");

          try {
            await benchmarkSaver.execute();

            console.clear();
            console.log("Benchmark saved successfuly.");
            await sleep(1000);

            console.clear();
          } catch (err) {
            console.log(err);
            process.exit(1);
          }

          break commandsLoop;
        }
        default:
          break;
      }
    }
  }

  async execute() {
    try {
      const testFiles = await new FilesReader(this.testsDir).execute();
      const testSuites = testFiles
        .map((testFile) => new TestReader(testFile).execute())
        .filter((testSuite) => !!testSuite);

      do {
        this.displayBenchmarkList(testSuites);
        const benchmark = await this.listenUntilBenchmarkSelected(testSuites);

        if (!benchmark) break;

        await this.executeBenchmark(benchmark);
      } while (true);
    } catch (err) {
      console.error(err);
      throw err;
    }
  }

  private displayBenchmarkList(testSuites: TestSuite[]) {
    console.log("\n");

    testSuites.forEach((testSuite, idx) => {
      console.log(
        `Press '${idx}' to run ${colors.bold.white(testSuite.title ?? testSuite.testFile.id)}`
      );
    });
    console.log(`Press '${testSuites.length}' to exit.`);
  }

  private displayCommands() {
    console.log("\n");
    console.log("Press 'r' to run benchmark again.");
    console.log("Press 'n' to return to benchmarks list.");
    console.log("Press 's' to save benchmark results.");
  }
}

export default BenchmarkTestRunnerController;
