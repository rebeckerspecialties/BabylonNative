import { spawnSync } from "node:child_process";
import { existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { join, resolve } from "node:path";

const repository = fileURLToPath(new URL("../../", import.meta.url));
const source = resolve(process.env.BABYLON_BUILD_SOURCE || join(repository, "build_babylon_bundle/source"));
const runner = join(source, "node_modules/vitest/vitest.mjs");
if (!existsSync(runner)) throw new Error("Build the maintained Babylon bundle first; its source and Vitest dependencies are required.");
const result = spawnSync(process.execPath, [runner, "run", "--config", fileURLToPath(new URL("./babylonUniformTests.config.mjs", import.meta.url)), "--configLoader", "native"], {
    cwd: source,
    env: { ...process.env, BABYLON_BUILD_SOURCE: source },
    stdio: "inherit",
});
if (result.error) throw result.error;
process.exitCode = result.status ?? 1;
