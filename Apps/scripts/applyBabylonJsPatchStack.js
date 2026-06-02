"use strict";

const fs = require("fs");
const os = require("os");
const path = require("path");
const { spawnSync } = require("child_process");

const appsDir = path.resolve(__dirname, "..");
const repoRoot = path.resolve(appsDir, "..");
const patchesDir = path.join(repoRoot, "Patches", "BabylonJS");
const seriesPath = path.join(patchesDir, "series");

function usage() {
    console.error(`Usage:
  node scripts/applyBabylonJsPatchStack.js --check [--babylon-js-dir <path>]
  node scripts/applyBabylonJsPatchStack.js --apply [--babylon-js-dir <path>]

Environment:
  BABYLON_JS_DIR  Overrides the default sibling Babylon.js checkout path.`);
}

function fail(message) {
    throw new Error(message);
}

function run(cwd, args, options = {}) {
    const result = spawnSync("git", args, {
        cwd,
        encoding: "utf8",
        stdio: options.quiet ? "pipe" : "inherit",
    });

    if (result.status !== 0) {
        if (options.quiet) {
            if (result.stdout) {
                process.stderr.write(result.stdout);
            }
            if (result.stderr) {
                process.stderr.write(result.stderr);
            }
        }
        fail(`git ${args.join(" ")} failed in ${cwd}`);
    }

    return result.stdout || "";
}

function gitResult(cwd, args) {
    return spawnSync("git", args, {
        cwd,
        encoding: "utf8",
        stdio: "pipe",
    });
}

function parseArgs(argv) {
    const options = {
        mode: "check",
        babylonJsDir: process.env.BABYLON_JS_DIR || path.resolve(repoRoot, "..", "Babylon.js"),
    };

    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i];
        if (arg === "--check") {
            options.mode = "check";
        } else if (arg === "--apply") {
            options.mode = "apply";
        } else if (arg === "--babylon-js-dir") {
            const value = argv[++i];
            if (!value) {
                usage();
                fail("--babylon-js-dir requires a path");
            }
            options.babylonJsDir = value;
        } else if (arg.startsWith("--babylon-js-dir=")) {
            options.babylonJsDir = arg.slice("--babylon-js-dir=".length);
        } else if (arg === "--help" || arg === "-h") {
            usage();
            process.exit(0);
        } else {
            usage();
            fail(`unknown argument: ${arg}`);
        }
    }

    options.babylonJsDir = path.resolve(options.babylonJsDir);
    return options;
}

function readSeries() {
    if (!fs.existsSync(seriesPath)) {
        fail(`missing patch series file: ${seriesPath}`);
    }

    const patches = fs
        .readFileSync(seriesPath, "utf8")
        .split(/\r?\n/)
        .map((line) => line.trim())
        .filter((line) => line.length > 0 && !line.startsWith("#"));

    if (patches.length === 0) {
        fail(`patch series is empty: ${seriesPath}`);
    }

    for (const patch of patches) {
        const patchPath = path.join(patchesDir, patch);
        if (!fs.existsSync(patchPath)) {
            fail(`missing patch listed by series: ${patchPath}`);
        }
    }

    return patches;
}

function ensureGitCheckout(dir) {
    if (!fs.existsSync(dir)) {
        fail(`Babylon.js checkout does not exist: ${dir}`);
    }
    run(dir, ["rev-parse", "--show-toplevel"], { quiet: true });
}

function ensureClean(dir) {
    const unstaged = spawnSync("git", ["diff", "--quiet"], { cwd: dir });
    const staged = spawnSync("git", ["diff", "--cached", "--quiet"], { cwd: dir });
    if (unstaged.status !== 0 || staged.status !== 0) {
        fail(`refusing to apply over local Babylon.js changes in ${dir}`);
    }
}

function applyPatch(dir, patch) {
    const patchPath = path.join(patchesDir, patch);
    const applyCheck = gitResult(dir, ["apply", "--check", "--whitespace=nowarn", patchPath]);
    if (applyCheck.status === 0) {
        console.log(`Applying ${patch}`);
        run(dir, ["apply", "--whitespace=nowarn", patchPath]);
        return;
    }

    const reverseCheck = gitResult(dir, ["apply", "--reverse", "--check", "--whitespace=nowarn", patchPath]);
    if (reverseCheck.status === 0) {
        console.log(`Already present ${patch}`);
        return;
    }

    if (applyCheck.stdout) {
        process.stderr.write(applyCheck.stdout);
    }
    if (applyCheck.stderr) {
        process.stderr.write(applyCheck.stderr);
    }
    if (reverseCheck.stdout) {
        process.stderr.write(reverseCheck.stdout);
    }
    if (reverseCheck.stderr) {
        process.stderr.write(reverseCheck.stderr);
    }
    fail(`patch does not apply and is not already present: ${patch}`);
}

function preflightApplyStack(targetDir, patches) {
    const tempRoot = fs.mkdtempSync(path.join(os.tmpdir(), "babylonjs-patch-stack-"));
    const tempWorktree = path.join(tempRoot, "Babylon.js");

    try {
        run(targetDir, ["worktree", "add", "--detach", "--quiet", tempWorktree, "HEAD"]);
        for (const patch of patches) {
            applyPatch(tempWorktree, patch);
        }
        run(tempWorktree, ["-c", "core.whitespace=blank-at-eol,blank-at-eof,space-before-tab,cr-at-eol", "diff", "--check"]);
    } finally {
        spawnSync("git", ["-C", targetDir, "worktree", "remove", "--force", tempWorktree], { stdio: "ignore" });
        fs.rmSync(tempRoot, { recursive: true, force: true });
    }
}

function main() {
    const options = parseArgs(process.argv.slice(2));
    const patches = readSeries();

    ensureGitCheckout(options.babylonJsDir);

    console.log(`Babylon.js checkout: ${options.babylonJsDir}`);
    console.log(`Patch stack: ${patches.length} patch(es) from ${patchesDir}`);

    preflightApplyStack(options.babylonJsDir, patches);
    console.log("Patch stack preflight passed.");

    if (options.mode === "apply") {
        ensureClean(options.babylonJsDir);
        for (const patch of patches) {
            applyPatch(options.babylonJsDir, patch);
        }
        console.log("Patch stack applied.");
    }
}

try {
    main();
} catch (error) {
    console.error(`error: ${error.message}`);
    process.exit(1);
}
