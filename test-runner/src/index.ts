import FilesReader from "./file-reader";
import path from "path";
import { exec } from "child_process";
import fs from "fs";

const TESTS_DIR = path.resolve(__dirname, ..."../../tests".split("/"));
const VM_PATH = path.resolve(__dirname, ..."../../build/simpl.exe".split("/"));

new FilesReader(TESTS_DIR).execute().then((paths) => {
  console.log(`${VM_PATH} ${paths[0]}`);
  console.log(process.cwd())

  exec(`"${VM_PATH}" "${paths[0]}"`, (error, stdout, stderr) => {
    if (error) {
      console.error(`Execution error: ${error.message}`);
      console.error(`Error code: ${error.code}`);
      console.error(`Signal received: ${error.signal}`);
      return;
    }

    if (stderr) {
      console.error(`stderr: ${stderr}`);
      return;
    }

    console.log(`stdout: ${stdout}`);
  });
});
