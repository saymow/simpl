const fs = require("fs");
const util = require("util");

const LINE_TERMINATOR_REGEX = /(?:\r\n|\n|\r)/;

const IN_PATHS = [
  "../src/priority-queue.inc",
  "../src/binary-tree.inc",
];
const OUT_PATH = "../src/modules-inc.h";

const readFile = util.promisify(fs.readFile);
const writeFile = util.promisify(fs.writeFile);

function escape(str) {
  return str.replace(/"/g, '\\"');
} 

async function main() {
  try {
    const fbufs = await Promise.all(IN_PATHS.map((path) => readFile(path)));
    const filesLines = fbufs.map((buf) => buf.toString().split(LINE_TERMINATOR_REGEX));
    let str = `// This file is generated from modules .inc files 

#ifndef MODULE_EXT
#define MODULE_EXT

char* modulesExtension =`;

    for (const fileLines of filesLines) {
      for (const line of fileLines) {
        str += `\n\t"${escape(line)}\\n"`;
      }
      str += `\n\t"\\n"`;
    }

    str += ";"

    str += `

#endif
`;

    await writeFile(OUT_PATH, str);
  } catch (err) {
    console.error("Could'nt generate modules-inc.h");
    console.error(err);
  }
}

main();
