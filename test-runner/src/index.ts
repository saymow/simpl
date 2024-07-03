import path from "path";
import colors from "colors";
import FilesReader from "./files-reader";
import TestSuiteReader, { TestSuite } from "./expectations-reader";
import TestRunner, { TestRunnerResult } from "./test-suite-runner";

const TESTS_DIR = path.resolve(__dirname, ..."../../tests".split("/"));
const VM_PATH = path.resolve(__dirname, ..."../../build/simpl.exe".split("/"));

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

const runStopwatch = () => {
  console.time(colors.white.bold("Time".padEnd(14, " ")));
};

const displayFinalResults = (results: FinalResult) => {
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

  console.timeEnd(colors.white.bold("Time".padEnd(14, " ")));
};

const run = async (testSuites: TestSuite[]) => {
  const results: FinalResult = {
    suites: { successes: 0, fails: 0 },
    assertions: { successes: 0, fails: 0 },
  };

  for (const testSuite of testSuites) {
    const suiteResults = await new TestRunner(VM_PATH, testSuite).execute();

    results.assertions.successes += suiteResults.successes;
    results.assertions.fails += suiteResults.fails;

    if (suiteResults.fails == 0) results.suites.successes++;
    else results.suites.fails++;
  }

  displayFinalResults(results);
};

const main = async () => {
  try {
    runStopwatch();
    const filesReader = new FilesReader(TESTS_DIR);
    const testFiles = await filesReader.execute();
    const testSuites: TestSuite[] = []; 
    
    for (const testFile of testFiles) { 
      const testSuitesReader = new TestSuiteReader(testFile);
      const testSuite = testSuitesReader.execute();

      // File is flagged to be skiped
      if (testSuite == null) continue;
      
      testSuites.push(testSuite);
    }

    await run(testSuites);
  } catch (err) {
    console.error(err);
    process.exit(1);
  }
};

main();
