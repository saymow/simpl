const fs = require("fs");
const util = require("util");

const LINE_TERMINATOR_REGEX = /(?:\r\n|\n|\r)/;

const IN_PATH = "../src/priority-queue.inc";
const OUT_PATH = "../src/modules-inc.h";

const readFile = util.promisify(fs.readFile);
const writeFile = util.promisify(fs.writeFile);

function escape(str) {
  return str.replace(/"/g, '\\"');
} 

async function main() {
  try {
    const file = await readFile(IN_PATH);
    const lines = file.toString().split(LINE_TERMINATOR_REGEX);
    let str = `// This file is generated from modules .inc files 

#ifndef MODULE_EXT
#define MODULE_EXT

char* modulesExtension =`;

    for (const line of lines) {
      str += `\n\t"${escape(line)}\\n"`;
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
