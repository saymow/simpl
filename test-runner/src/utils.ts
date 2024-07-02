import fs from "fs";
import path from "path";
import { promisify } from "util";

export const LINE_TERMINATOR_REGEX = /(?:\r\n|\n|\r)/;

export const readFile = promisify(fs.readFile);

export const crawlFilePaths = async (dir: string): Promise<string[]> => {
  const result: string[] = [];
  const filenames = await readdir(dir);
  const filenamesAbsPath = filenames.map((filename) =>
    path.resolve(dir, filename)
  );

  for (const filename of filenamesAbsPath) {
    if ((await readstat(filename)).isDirectory()) {
      result.push(...(await crawlFilePaths(filename)));
    } else {
      result.push(filename);
    }
  }

  return result;
};

const readdir = promisify(fs.readdir);

const readstat = promisify(fs.stat);
