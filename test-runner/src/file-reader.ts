import { crawlFilenames, readFile } from "./utils";

class FilesReader {
  constructor(private readonly startPath: string) {}

  async execute(): Promise<string[]> {
    try {
      const filenames = crawlFilenames(this.startPath);

      return filenames;
    } catch (err) {
      console.error("Cannot read files: ", err);
      throw err;
    }
  }
}

export default FilesReader;
