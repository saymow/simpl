import readline from "readline";
// @ts-ignore
import argsParser from "args-parser";
import { Mode, Settings, TestRunnerController } from "./interfaces";
import StandardTestRunnerController from "./standard-test-runner-controller";
import BenchmarkTestRunnerController from "./benchmark-test-runner-controller";

readline.emitKeypressEvents(process.stdin);

if (process.stdin.isTTY) process.stdin.setRawMode(true);

const parseSettings = (): Settings => {
  const args = argsParser(process.argv);

  if ("help" in args || !("vm" in args) || !("tests" in args)) {
    console.log("usage: node index.js <command>");
    console.log("\n");
    console.log("--vm".padEnd(24) + "Virtual machine executable path.");
    console.log("--tests".padEnd(24) + "Tests directory.");
    console.log(
      "--benchmark-mode".padEnd(24) + "(Optional) Run in benchmark mode."
    );
    process.exit(1);
  }

  return {
    vm: args.vm,
    tests: args.tests,
    mode: args["benchmark-mode"] ? Mode.Benchmark : Mode.Standard,
  };
};

const makeTestRunnerController = (settings: Settings): TestRunnerController => {
  if (settings.mode === Mode.Benchmark) {
    return new BenchmarkTestRunnerController(settings);
  }

  return new StandardTestRunnerController(settings);
}

const main = async () => {
  const settings = parseSettings();

  await makeTestRunnerController(settings).execute();
};

main();