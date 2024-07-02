import path from "path";
import FilesReader from "./files-reader";
import TestSuiteReader, { TestSuite } from "./expectations-reader";
import TestRunner from "./test-suite-runner";

const TESTS_DIR = path.resolve(__dirname, ..."../../tests".split("/"));
const VM_PATH = path.resolve(__dirname, ..."../../build/simpl.exe".split("/"));

const run = async (testSuites: TestSuite[]) => {
  for (const testSuite of testSuites) {
    await new TestRunner(VM_PATH, testSuite).execute();
  }
};

const main = async () => {
  try {
    const filesReader = new FilesReader(TESTS_DIR);
    const testFiles = await filesReader.execute();
    const testSuites = testFiles.map((testFile) => {
      const testSuitesReader = new TestSuiteReader(testFile);
      return testSuitesReader.execute();
    });

    await run(testSuites);
  } catch (err) {
    console.error(err);
    process.exit(1);
  }
};

main();
