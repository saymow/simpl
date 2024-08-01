import { exec } from "child_process";
import { TestSuite } from "./test-reader";
import { VmErrors } from "./standard-test-reader";
import colors from "colors";
import { LINE_TERMINATOR_REGEX } from "./utils";

const BENCHMARK_ROUNDS = 3;

export interface BenchmarkResults {
  [benchmarkRound: string]: string;
  min: string;
  max: string;
  median: string;
  avg: string;
  var: string;
  stdDeviation: string;
  rounds: string;
}

class BenchmarkError extends Error {}

class BenchmarkTestSuiteRunner {
  constructor(
    private readonly vmPath: string,
    private readonly testSuite: TestSuite
  ) {}

  private buildBenchmarkResults(elapsedTimes: number[]): BenchmarkResults {
    const orderedElapsedTimes = elapsedTimes.sort((a, b) => a - b);
    const results: Record<string, any> = {};

    results.rounds = elapsedTimes.length;

    orderedElapsedTimes.reduce((acc, result, resultIndex) => {
      acc[`round ${resultIndex}`] = result;

      return acc;
    }, results);

    results.min = orderedElapsedTimes[0];
    results.max = orderedElapsedTimes[orderedElapsedTimes.length - 1];
    results.median = elapsedTimes[Math.ceil((elapsedTimes.length - 1) / 2)];
    results.avg =
      elapsedTimes.reduce((acc, elapsedTime) => acc + elapsedTime) /
      elapsedTimes.length;
    results.stdDeviation = Math.sqrt(
      elapsedTimes.reduce(
        (acc, elapsedTime) => acc + (elapsedTime - results.avg) ** 2,
        0
      ) / elapsedTimes.length
    );

    for (const key of Object.keys(results)) {
      if (key === "rounds") continue;
      results[key] = results[key].toFixed(3) + "s";
    }

    return results as BenchmarkResults;
  }

  private async benchmark(): Promise<number> {
    const paths = `"${this.vmPath}" "${this.testSuite.testFile.path}"`;
    const testTitle = this.testSuite.title;
    const fileId = this.testSuite.testFile.id;
    let errorMessage = "";

    return new Promise<number>((resolve, reject) => {
      exec(paths, (error, stdout, stderr) => {
        const stdOutLines = stdout.split(LINE_TERMINATOR_REGEX);
        const stdErrLines = stderr.split(LINE_TERMINATOR_REGEX);

        // Remove trailing line terminator
        stdOutLines.pop();
        stdErrLines.pop();

        if (error) {
          const errorCode = this.parseErrorCode(error.code ?? -1);

          errorMessage += `${colors.bgRed.bold("FAIL")} ${colors.bold(
            fileId
          )}\n\n`;

          if (testTitle) {
            errorMessage += colors.red.bold(`\t● › ${testTitle}\n`);
          }

          if (errorCode == null) {
            errorMessage += `Unexpected Execution error: ${error.message}\n`;
            errorMessage += `Error code: ${error.code}\n`;
            errorMessage += `Signal received: ${error.signal}`;

            return reject(new BenchmarkError(errorMessage));
          } else {
            for (const stderrLine of stdErrLines) {
              errorMessage += `\t${stderrLine}\n`;
            }

            if (stdErrLines.length) {
              errorMessage += "\t\n";
            }
          }

          return reject(new BenchmarkError(errorMessage));
        }

        if (stdOutLines.length === 0) {
          errorMessage += `${colors.bgRed.bold("FAIL")} ${colors.bold(
            fileId
          )}\n\n`;

          if (testTitle) {
            errorMessage += colors.red.bold(`\t● › ${testTitle}\n`);
          }

          errorMessage += colors.black(
            "\t Expect benchmark test to print elapsed time.\n"
          );

          return reject(new BenchmarkError(errorMessage));
        } else {
          const elapsedTime = parseFloat(stdOutLines[stdOutLines.length - 1]);

          return resolve(elapsedTime);
        }
      });
    });
  }

  private displayBenchmarkResults(results: BenchmarkResults) {
    const testTitle = this.testSuite.title;
    const fileId = this.testSuite.testFile.id;

    console.log(`${colors.bgGreen.bold("PASS")} ${colors.bold(fileId)}`);

    if (testTitle) {
      console.log(colors.green.bold(`● › ${testTitle}`));
    }

    console.log("\t");

    console.table(results);
  }

  async execute() {
    try {
      const elapsedTimes: number[] = [];

      for (let idx = 0; idx < BENCHMARK_ROUNDS; idx++) {
        console.clear();
        console.log(`${colors.bgWhite.bold("RUNNING")} ${colors.bold(this.testSuite.testFile.id)}`);
        console.log(colors.white.bold(`● › ${this.testSuite.title}\n`));
        console.log(`Running round ${idx + 1}/${BENCHMARK_ROUNDS}...`);
        elapsedTimes.push(await this.benchmark());
      }

      console.clear();

      const results = this.buildBenchmarkResults(elapsedTimes);

      this.displayBenchmarkResults(results);

      return results;
    } catch (err) {
      if (err instanceof BenchmarkError) {
        console.log(err.message);
        throw err;
      }

      console.error("Unexpected error: ", err);
      throw err;
    }
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

export default BenchmarkTestSuiteRunner;
