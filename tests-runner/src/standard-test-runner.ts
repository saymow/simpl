import colors from "colors";
import FilesReader from "./files-reader";
import StandardTestReader, { StandardTestSuite } from "./standard-test-reader";
import TestSuiteRunner from "./standard-test-suite-runner";

interface FinalResult {
  suites: {
    successes: number;
    fails: number;
  };
  assertions: {
    successes: number;
    fails: number;
  };
}

class StandardTestRunner {
  constructor(
    private readonly vmPath: string,
    private readonly testsDir: string,
    private readonly skipAssertions: boolean
  ) {}

  runStopwatch() {
    console.time(colors.white.bold("Time".padEnd(14, " ")));
  }

  displayFinalResults(results: FinalResult) {
    console.log("\n");

    console.log(
      colors.white.bold("Test suites ".padEnd(14, " ") + ": ") +
        (results.suites.fails
          ? colors.red.bold(`${results.suites.fails} failed`) + ", "
          : "") +
        (results.suites.successes
          ? colors.green.bold(`${results.suites.successes} passed`) + ", "
          : "") +
        `${results.suites.successes + results.suites.fails} total`
    );

    if (this.skipAssertions) {
      console.log(
      colors.white.bold("Assertions ".padEnd(14, " ") + ": ") +
        (results.assertions.fails
          ? colors.red.bold(`${results.assertions.fails} failed`) + ", "
          : "") +
        (results.assertions.successes
          ? colors.green.bold(`${results.assertions.successes} passed`) + ", "
          : "") +
        `${results.assertions.successes + results.assertions.fails} total`
    );
    }

    console.timeEnd(colors.white.bold("Time".padEnd(14, " ")));
  }

  async run(testSuites: StandardTestSuite[]): Promise<StandardTestSuite[]> {
    const failedTests: StandardTestSuite[] = [];
    const results: FinalResult = {
      suites: { successes: 0, fails: 0 },
      assertions: { successes: 0, fails: 0 },
    };

    for (const testSuite of testSuites) {
      const testSuiteRunner = new TestSuiteRunner(this.vmPath, testSuite);
      const suiteResults = await (this.skipAssertions
        ? testSuiteRunner.softExecute()
        : testSuiteRunner.execute()
      );

      results.assertions.successes += suiteResults.successes;
      results.assertions.fails += suiteResults.fails;

      if (suiteResults.fails == 0) results.suites.successes++;
      else {
        failedTests.push(testSuite);
        results.suites.fails++;
      }
    }

    this.displayFinalResults(results);

    return failedTests;
  }

  async executeTestSuites(testSuites: StandardTestSuite[]) {
    try {
      this.runStopwatch();
      return this.run(testSuites);
    } catch (err) {
      console.error(err);
    }
  }

  async execute() {
    try {
      this.runStopwatch();
      const filesReader = new FilesReader(this.testsDir);
      const testFiles = await filesReader.execute();
      const testSuites: StandardTestSuite[] = [];

      for (const testFile of testFiles) {
        const testSuitesReader = new StandardTestReader(testFile);
        const testSuite = testSuitesReader.execute();

        // File is flagged to be skiped
        if (testSuite == null) continue;

        testSuites.push(testSuite);
      }

      return this.run(testSuites);
    } catch (err) {
      console.error(err);
    }
  }
}

export default StandardTestRunner;
