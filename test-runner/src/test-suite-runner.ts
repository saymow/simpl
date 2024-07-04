import { exec } from "child_process";
import {
  Assertion,
  ExpectAssertion,
  TestSuite,
  VmErrors,
} from "./expectations-reader";
import { LINE_TERMINATOR_REGEX } from "./utils";
import colors from "colors";

export interface TestRunnerResult {
  successes: number;
  fails: number;
}

class TestRunner {
  private successes: number = 0;
  private fails: number = 0;
  private errorMessages: string[] = [];

  constructor(
    private readonly vmPath: string,
    private readonly testSuite: TestSuite
  ) {}

  private error(message: string) {
    this.errorMessages.push(`${this.testSuite.testFile.id}: ${message}`);
  }

  private expectAssertionError(assertion: ExpectAssertion, stdOutline: string) {
    let message;

    if (assertion.data !== null && stdOutline !== undefined) {
      message = `Expected "${assertion.data}" but received "${stdOutline}".`;
    } else if (assertion.data === null) {
      message = `Expected to not receive but received "${stdOutline}"`;
    } else {
      message = `Expected "${assertion.data}" but did not receive.`;
    }

    this.errorMessages.push(`[line ${assertion.line}]: ${message}`);
  }

  private assertionError(assertion: Assertion<any>, message: string) {
    this.errorMessages.push(`[line ${assertion.line}]: ${message}`);
  }

  private evalute(assertion: ExpectAssertion, stdOutline: string) {
    if (assertion.data === null && stdOutline === undefined) return true;

    return assertion.data === stdOutline;
  }

  async execute(): Promise<TestRunnerResult> {
    const paths = `"${this.vmPath}" "${this.testSuite.testFile.path}"`;
    const testTitle = this.testSuite.title;
    const fileId = this.testSuite.testFile.id;
    const expects = this.testSuite.expectation.expects;
    const expectedError = this.testSuite.expectation.error;

    return new Promise<TestRunnerResult>((resolve, reject) => {
      exec(paths, (error, stdout, stderr) => {
        if (error && this.parseErrorCode(error.code ?? -1) == null) {
          console.error(`Unexpected Execution error: ${error.message}`);
          console.error(`Error code: ${error.code}`);
          console.error(`Signal received: ${error.signal}`);
          reject(error);
        }

        const stdOutLines = stdout.split(LINE_TERMINATOR_REGEX);
        const stdErrLines = stderr.split(LINE_TERMINATOR_REGEX);

        // Remove trailing line terminator
        stdOutLines.pop();
        stdErrLines.pop();

        for (let idx = 0; idx < expects.length; idx++) {
          if (this.evalute(expects[idx], stdOutLines[idx])) {
            this.successes++;
          } else {
            this.expectAssertionError(expects[idx], stdOutLines[idx]);
            this.fails++;
          }
        }

        if (error) {
          const vmError = this.parseErrorCode(error.code ?? -1);

          if (expectedError) {
            if (expectedError.data.error === vmError) {
              if (expectedError.data.message) {
                if (expectedError.data.message === stdErrLines[0]) {
                  this.successes++;
                } else {
                  this.fails++;
                  this.assertionError(
                    expectedError,
                    `Expected "${expectedError.data.message}" error message but received "${stdErrLines[0]}".`
                  );
                }
              } else {
                this.successes++;
              }
            } else {
              this.fails++;
              this.assertionError(
                expectedError,
                `Expected ${expectedError.data.error} but received ${vmError}.`
              );
            }
          } else {
            this.fails++;
            this.error(`Unhandled error ${vmError}.`);
          }
        }

        if (this.fails === 0) {
          console.log(`${colors.bgGreen.bold("PASS")} ${colors.bold(fileId)}`);
        } else {
          console.log(`${colors.bgRed.bold("FAIL")} ${colors.bold(fileId)}`);

          if (testTitle) {
            console.log(colors.red.bold(`\t● › ${testTitle}\n`));
          }

          if (stderr) {
            const stderrLines = stderr.split(LINE_TERMINATOR_REGEX);

            for (const stderrLine of stderrLines) {
              console.log(`\t${stderrLine}`);
            }
          }

          for (const errorMessage of this.errorMessages) {
            console.log(colors.black(`\t${errorMessage}`));
          }
        }

        resolve({ successes: this.successes, fails: this.fails });
      });
    });
  }

  private parseErrorCode(code: number): VmErrors | null {
    switch (code) {
      case 1:
        return VmErrors.ERR;
      case 65:
        return VmErrors.DATA_ERR;
      case 70:
        return VmErrors.SOFTWARE_ERR;
      default:
        return null;
    }
  }
}

export default TestRunner;
