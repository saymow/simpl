import fs from "fs";
import path from "path";
import { promisify } from "util";

export const readFile = promisify(fs.readFile);

const readdir = promisify(fs.readdir);

const readstat = promisify(fs.stat);

export const crawlFilenames = async (dir: string): Promise<string[]> => {
  const result: string[] = [];
  const filenames = await readdir(dir);
  const filenamesAbsPath = filenames.map((filename) =>
    path.resolve(dir, filename)
  );

  for (const filename of filenamesAbsPath) {
    if ((await readstat(filename)).isDirectory()) {
      result.push(...(await crawlFilenames(filename)));
    } else {
      result.push(filename);
    }
  }

  return result;
};
