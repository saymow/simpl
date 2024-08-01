import { StandardTestSuite } from "./standard-test-reader";
import { Settings, TestRunnerController } from "./interfaces";
import StandardTestRunner from "./standard-test-runner";

class StandardTestRunnerController extends TestRunnerController {
  private is_running = false;
  private failedTestSuites: StandardTestSuite[] = [];
  private skipAssertions: boolean;

  constructor(settings: Settings, skipAssertions: boolean) {
    super(settings);
    this.skipAssertions = skipAssertions;
  }

  private async run() {
    if (this.is_running) return;

    this.is_running = true;
    const testsRunner = new StandardTestRunner(
      this.vmPath,
      this.testsDir,
      this.skipAssertions
    );
    this.failedTestSuites =
      (await testsRunner.execute()) ?? this.failedTestSuites;

    this.displayCommands();
    this.is_running = false;
  }

  private async runFailed() {
    if (this.is_running) return;
    if (this.failedTestSuites.length === 0) return;

    this.is_running = true;
    const testsRunner = new StandardTestRunner(
      this.vmPath,
      this.testsDir,
      this.skipAssertions
    );
    this.failedTestSuites =
      (await testsRunner.executeTestSuites(this.failedTestSuites)) ??
      this.failedTestSuites;

    this.displayCommands();
    this.is_running = false;
  }

  async execute() {
    await this.run();

    process.stdin.on("keypress", async (_, key) => {
      if (key.name === "a") {
        await this.run();
      } else if (key.name === "f") {
        await this.runFailed();
      } else if (key.name === "c" && key.ctrl) {
        process.exit(0);
      }
    });
  }

  private displayCommands() {
    console.log("\n");
    console.log("Press 'a' to run all tests.");
    console.log("Press 'f' to run failed tests.");
  }
}

export default StandardTestRunnerController;
