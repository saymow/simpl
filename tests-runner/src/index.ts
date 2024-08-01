import readline from "readline";
// @ts-ignore
import argsParser from "args-parser";
import { Mode, Settings, TestRunnerController } from "./interfaces";
import StandardTestRunnerController from "./standard-test-runner-controller";
import BenchmarkTestRunnerController from "./benchmark-test-runner-controller";

readline.emitKeypressEvents(process.stdin);

if (process.stdin.isTTY) process.stdin.setRawMode(true);

const displayHelp = () => {
  console.log("usage: node index.js <command>");
  console.log("\n");
  console.log("--vm".padEnd(24) + "Virtual machine executable path.");
  console.log("--tests".padEnd(24) + "Tests directory.");
  console.log(
    "--mode=<mode>".padEnd(24) +
      '(Optional) - modes: "standard" (default), "benchmark" and "run".'
  );
  process.exit(1);
}

const parseSettings = (): Settings => {
  const args = argsParser(process.argv);

  if ("help" in args || !("vm" in args) || !("tests" in args)) {
    displayHelp();
    process.exit(1);
  }

  if (!("mode" in args)) {
    args.mode = Mode.Standard;
  } else {
    switch (args.mode) {
      case "standard":
        args.mode = Mode.Standard;
        break;
      case "benchmark":
        args.mode = Mode.Benchmark;
        break;
      case "run":
        args.mode = Mode.Run;
        break;
      default: {
        displayHelp();
        process.exit(1);
      }
    }
  }

  return { vm: args.vm, tests: args.tests, mode: args.mode };
};

const makeTestRunnerController = (settings: Settings): TestRunnerController => {
  if (settings.mode === Mode.Benchmark) {
    return new BenchmarkTestRunnerController(settings);
  } else if (settings.mode = Mode.Run) {
    return new StandardTestRunnerController(settings, true);
  }

  return new StandardTestRunnerController(settings);
};

const main = async () => {
  const settings = parseSettings();

  await makeTestRunnerController(settings).execute();
};

main();
