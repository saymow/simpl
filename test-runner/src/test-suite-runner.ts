import { exec } from "child_process";
import { TestSuite, VmErrors } from "./expectations-reader";
import { LINE_TERMINATOR_REGEX } from "./utils";
import colors from "colors";

class TestRunner {
  private successes: number = 0;
  private fails: number = 0;
  private errorMessages: string[] = [];

  constructor(
    private readonly vmPath: string,
    private readonly testSuite: TestSuite
  ) {}

  private assertionError(line: number, message: string) {
    this.errorMessages.push(`[line ${line}]: ${message}`);
  }

  private error(message: string) {
    this.errorMessages.push(`${this.testSuite.testFile.id}: ${message}`);
  }

  async execute(): Promise<void> {
    const paths = `"${this.vmPath}" "${this.testSuite.testFile.path}"`;
    const testTitle = this.testSuite.title;
    const fileId = this.testSuite.testFile.id;
    const expects = this.testSuite.expectation.expects;
    const expectedError = this.testSuite.expectation.error;

    await new Promise<void>((resolve, reject) => {
      exec(paths, (error, stdout, stderr) => {
        if (error && this.parseErrorCode(error.code ?? -1) == null) {
          console.error(`Unexpected Execution error: ${error.message}`);
          console.error(`Error code: ${error.code}`);
          console.error(`Signal received: ${error.signal}`);
          reject(error);
        }

        const stdOutLines = stdout.split(LINE_TERMINATOR_REGEX);

        for (let idx = 0; idx < expects.length; idx++) {
          if (expects[idx].data === stdOutLines[idx]) {
            this.successes++;
          } else {
            this.assertionError(
              expects[idx].line,
              `Expected ${expects[idx].data} but received ${stdOutLines[idx]}.`
            );
            this.fails++;
          }
        }

        if (error) {
          const vmError = this.parseErrorCode(error.code ?? -1);

          if (expectedError) {
            if (expectedError.data === vmError) {
              this.successes++;
            } else {
              this.fails++;
              this.assertionError(
                expectedError.line,
                `Expected ${expectedError.data} but received ${vmError}.`
              );
            }
          } else {
            this.fails++;
            this.error(`Unhandled error ${vmError}.`);
          }
        }

        if (this.fails === 0 && !stderr) {
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

        resolve();
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
