import { TestFile } from "./files-reader";
import TestReader, { TestSuite } from "./test-reader";

const EXPECT_REGEX = /(?<=\/\/\sexpect\s).+/;
const EXPECT_VOID_REGEX = /(?<=\s*)!void/;
const ERROR_REGEX = /(?<=\/\/\serror\s).+(\s)?.*/;

export enum VmErrors {
  ERR = "ERR",                                    // exit code: 1
  DATA_ERR = "DATA_ERR",                          // exit code: 65
  SOFTWARE_ERR = "SOFTWARE_ERR",                  // exit code: 70
}

export interface Assertion<T> {
  line: number;
  data: T;
}

export type ExpectAssertion = Assertion<string | null>;

export type ErrorAssertion = Assertion<{
  error: VmErrors;
  message?: string;
}>;

export interface Expectations {
  expects: ExpectAssertion[];
  error?: ErrorAssertion;
}

export interface ExpectationTestSuite extends TestSuite {
  expectation: Expectations;
}

class ExpectationTestReader extends TestReader {
  private readonly expectations: Expectations = { expects: [] };

  private resolveErrorTest(
    line: number,
    expectMessage: string
  ): ErrorAssertion {
    const [error, ...message] = expectMessage.split(" ");

    if (!Object.values(VmErrors).includes(error as any)) {
      throw this.error(line, "invalid error type.");
    }
    if (this.expectations.error) {
      throw this.error(line, "can only test for error once.");
    }

    return {
      line,
      data: { error: error as VmErrors, message: message.join(" ") },
    };
  }

  execute(): ExpectationTestSuite | null {
    if (this.shouldSkip) return null;

    for (let idx = 0; idx < this.lines.length; idx++) {
      const text = this.lines[idx];
      const line = idx + 1;
      const expectTest = EXPECT_REGEX.exec(text);

      if (expectTest) {
        const voidTest = EXPECT_VOID_REGEX.test(expectTest[0]);

        if (voidTest) {
          this.expectations.expects.push({ data: null, line });
          continue;
        }

        this.expectations.expects.push({ data: expectTest[0], line });
        continue;
      }

      const errorTest = ERROR_REGEX.exec(text);

      if (errorTest) {
        this.expectations.error = this.resolveErrorTest(line, errorTest[0]);
        continue;
      }
    }

    return {
      title: this.title,
      testFile: this.testFile,
      expectation: this.expectations,
    };
  }
}

export default ExpectationTestReader;
