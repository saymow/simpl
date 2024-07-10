export abstract class TestRunnerController {
  readonly vmPath: string;
  readonly testsDir: string;

  constructor(settings: Settings) {
    this.vmPath = settings.vm;
    this.testsDir = settings.tests;
  }

  public abstract execute():  Promise<void>;
}

export enum Mode {
  Standard,
  Benchmark,
}

export interface Settings {
  vm: string;
  tests: string;
  mode: Mode;
}
