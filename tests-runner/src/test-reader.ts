import { TestFile } from "./files-reader";
import { LINE_TERMINATOR_REGEX } from "./utils";

const COMMENTARY_REGEX = /(?<=\/\/\s).+/;
const SKIP_FILE_TITLE_REGEX = /!skip/;

export interface TestSuite {
  title?: string;
  testFile: TestFile;
}

class TestReader {
  readonly title: string | undefined;
  readonly lines: string[] = [];
  readonly shouldSkip: boolean = false;

  constructor(readonly testFile: TestFile) {
    this.lines = this.testFile.source.split(LINE_TERMINATOR_REGEX);

    if (this.lines.length) {
      const testTitle = COMMENTARY_REGEX.exec(this.lines[0]);

      if (testTitle) {
        const skipFileTitle = SKIP_FILE_TITLE_REGEX.exec(testTitle[0]);

        this.shouldSkip = !!skipFileTitle;
        if (!this.shouldSkip) {
          this.title = testTitle[0];
        }
      }
    }
  }

  error(line: number, message: string): Error {
    return new Error(`${this.testFile.id} [line ${line}]: ${message}`);
  }

  execute(): TestSuite | null {
    if (this.shouldSkip) return null;

    return { title: this.title, testFile: this.testFile };
  }
}

export default TestReader;
