import BenchmarkTestSaver from "./benchmark-test-saver";
import BenchmarkTestSuiteRunner, {
  BenchmarkResults,
} from "./benchmark-test-suite-runner";
import FilesReader from "./files-reader";
import { Settings, TestRunnerController } from "./interfaces";
import TestReader, { TestSuite } from "./test-reader";
import { sleep } from "./utils";

enum TestCommand {
  Repeat,
  Next,
  Save,
}

class BenchmarkTestRunnerController extends TestRunnerController {
  constructor(settings: Settings) {
    super(settings);
  }

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
          resolve(TestCommand.Next);
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

  async execute() {
    try {
      const testFiles = await new FilesReader(this.testsDir).execute();

      for (const testFile of testFiles) {
        const testSuiteReader = new TestReader(testFile);
        const testSuite = testSuiteReader.execute();

        // File is flagged to be skiped
        if (testSuite == null) continue;

        const testSuiteRunner = new BenchmarkTestSuiteRunner(
          this.vmPath,
          testSuite
        );

        commandLoop: while (true) {
          const benchmarkResult = await testSuiteRunner.execute();

          this.displayCommands();

          const command = await this.listenUntilCommand();

          switch (command) {
            case TestCommand.Repeat: {
              console.clear();
              break;
            }
            case TestCommand.Next: {
              console.clear();
              break commandLoop;
            }
            case TestCommand.Save: {
              console.clear();
              console.log("Benchmark title: ");

              const benchmarkTitle = await this.readLine();
              const benchmarkSaver = new BenchmarkTestSaver(
                benchmarkTitle,
                testFile,
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
              }

              break;
            }
            default:
              break;
          }
        }
      }
    } catch (err) {
      console.error(err);
    }
  }

  private displayCommands() {
    console.log("\n");
    console.log("Press 'r' to run benchmark again.");
    console.log("Press 'n' to go to next benchmark.");
    console.log("Press 's' to save benchmark results.");
  }
}

export default BenchmarkTestRunnerController;
