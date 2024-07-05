import readline from "readline";
import path from "path";
import TestRunnerSuite from "./test-suite-runner";
import TestRunner from "./test-runner";
import { TestSuite } from "./expectations-reader";

readline.emitKeypressEvents(process.stdin);

if (process.stdin.isTTY) process.stdin.setRawMode(true);

const TESTS_DIR = path.resolve(__dirname, ..."../../tests".split("/"));
const VM_PATH = path.resolve(__dirname, ..."../../build/simpl.exe".split("/"));

const testsRunner = new TestRunner(VM_PATH, TESTS_DIR);

let is_running = false;
let faieldTestSuites: TestSuite[] = [];

const displayCommands = () => {
  console.log("\n");
  console.log("Press 'a' to run all tests.");
  console.log("Press 'f' to run failed tests.");
}

const run = async () => {
  if (is_running) return;

  is_running = true;
  faieldTestSuites = (await testsRunner.execute()) ?? faieldTestSuites;

  displayCommands();
  is_running = false;
};

const runFailed = async () => {
  if (is_running) return;
  if (faieldTestSuites.length === 0) return;

  is_running = true;
  faieldTestSuites = (await testsRunner.executeTestSuites(faieldTestSuites)) ?? faieldTestSuites;

  displayCommands();
  is_running = false;
};

const main = async () => {
  await run();

  process.stdin.on("keypress", async (_, key) => {
    if (key.name === 'a') {
      await run();
    } else if (key.name === 'f') {
      await runFailed();
    } else if (key.name === 'c' && key.ctrl) {
      process.exit(0);
    }
  });
};

main();
