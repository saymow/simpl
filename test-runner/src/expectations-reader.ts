import { TestFile } from "./files-reader";
import { LINE_TERMINATOR_REGEX } from "./utils";

const COMMENTARY_REGEX = /(?<=\/\/\s).+/;
const SKIP_FILE_TITLE_REGEX = /!skip/;
const EXPECT_REGEX = /(?<=\/\/\sexpect\s).+/;
const EXPECT_VOID_REGEX = /(?<=\s*)!void/;
const ERROR_REGEX = /(?<=\/\/\serror\s).+?(?=\s|$)/;

export enum VmErrors {
  ERR = "ERR", // exit code: 1
  DATA_ERR = "DATA_ERR", // exit code: 65
  SOFTWARE_ERR = "SOFTWARE_ERR", // exit code: 70
}

interface Assertion<T> {
  line: number;
  data: T;
}

export type ExpectAssertion = Assertion<string | null>;

interface Expectations {
  expects: ExpectAssertion[];
  error?: Assertion<VmErrors>;
}

export interface TestSuite {
  title?: string;
  testFile: TestFile;
  expectation: Expectations;
}

class TestSuiteReader {
  private title: string | undefined;
  private readonly expectations: Expectations = { expects: [] };

  constructor(private testFile: TestFile) {}

  private error(line: number, message: string): Error {
    return new Error(`${this.testFile.id} [line ${line}]: ${message}`);
  }

  execute(): TestSuite | null {
    const lines = this.testFile.source.split(LINE_TERMINATOR_REGEX);
    let idx = 0;

    if (lines.length) {
      const testTitle = COMMENTARY_REGEX.exec(lines[idx]);

      if (testTitle) {
        const skipFileTitle = SKIP_FILE_TITLE_REGEX.exec(testTitle[0]);

        if (skipFileTitle) {
          return null;
        }

        this.title = testTitle[0];
        idx++;
      }
    }

    for (idx = 0; idx < lines.length; idx++) {
      const text = lines[idx];
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
        if (!Object.values(VmErrors).includes(errorTest[0] as any)) {
          throw this.error(line, "invalid error type.");
        }
        if (this.expectations.error) {
          throw this.error(line, "can only test for error once.");
        }

        this.expectations.error = { data: errorTest[0] as VmErrors, line };
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

export default TestSuiteReader;
