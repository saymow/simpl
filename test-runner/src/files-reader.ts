import { crawlFilePaths, readFile } from "./utils";

export interface TestFile {
  id: string;
  path: string;
  source: string;
}

class FilesReader {
  constructor(private readonly startPath: string) {}

  async execute(): Promise<TestFile[]> {
    try {
      const filesPaths = await crawlFilePaths(this.startPath);
      const response: TestFile[] = await Promise.all(
        filesPaths.map(async (filePath) => ({
          id: filePath.replace(this.startPath + "\\", ""),
          path: filePath,
          source: (await readFile(filePath)).toString(),
        }))
      );

      return response;
    } catch (err: any) {
      throw new Error("Cannot read test files: " + err.message);
    }
  }
}

export default FilesReader;
