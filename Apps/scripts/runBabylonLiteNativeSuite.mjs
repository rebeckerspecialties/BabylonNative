#!/usr/bin/env node

import { existsSync, readdirSync, writeFileSync } from "node:fs";
import path from "node:path";
import { spawn } from "node:child_process";

function parseArguments(argv) {
    const options = { scenes: null, timeoutMs: 90000 };
    for (let index = 0; index < argv.length; ++index) {
        const argument = argv[index];
        if (argument === "--playground") {
            options.playground = argv[++index];
        } else if (argument === "--bundle-dir") {
            options.bundleDir = argv[++index];
        } else if (argument === "--scenes") {
            options.scenes = argv[++index].split(",").map((value) => value.trim()).filter(Boolean);
        } else if (argument === "--timeout-ms") {
            options.timeoutMs = Number(argv[++index]);
        } else if (argument === "--report") {
            options.report = argv[++index];
        } else {
            throw new Error(`Unknown argument: ${argument}`);
        }
    }
    if (!options.playground || !options.bundleDir) {
        throw new Error("Usage: runBabylonLiteNativeSuite.mjs --playground PATH --bundle-dir PATH [--scenes 1,2] [--timeout-ms N] [--report PATH]");
    }
    if (!(options.timeoutMs > 0)) {
        throw new Error("--timeout-ms must be positive.");
    }
    return options;
}

function normalizeSceneName(value) {
    const name = String(value).startsWith("scene") ? String(value) : `scene${value}`;
    if (!/^scene\d+$/.test(name)) {
        throw new Error(`Invalid scene name: ${value}`);
    }
    return name;
}

function runScene(options, scene) {
    return new Promise((resolve) => {
        const args = [
            "--headless",
            "--save-results",
            "true",
            "app:///Scripts/babylon_lite_bootstrap.js",
            `app:///Scripts/BabylonLite/${scene}.playground.js`,
            "app:///Scripts/babylon_lite_validation.js"
        ];
        const child = spawn(options.playground, args, {
            env: process.env,
            stdio: ["ignore", "pipe", "pipe"]
        });
        let output = "";
        let timedOut = false;
        const timer = setTimeout(() => {
            timedOut = true;
            child.kill("SIGTERM");
            setTimeout(() => child.kill("SIGKILL"), 2000).unref();
        }, options.timeoutMs);

        child.stdout.on("data", (data) => {
            output += data.toString();
        });
        child.stderr.on("data", (data) => {
            output += data.toString();
        });
        child.on("error", (error) => {
            clearTimeout(timer);
            resolve({ scene, status: "launch-error", exitCode: null, signal: null, error: String(error), output });
        });
        child.on("exit", (exitCode, signal) => {
            clearTimeout(timer);
            const marker = output.match(/BABYLON_LITE_NATIVE_RESULT\s+(\{[^\r\n]+\})/);
            let result = null;
            if (marker) {
                try {
                    result = JSON.parse(marker[1]);
                } catch {
                    result = null;
                }
            }
            const exitStatuses = new Map([
                [10, "console-error"],
                [11, "uniform-frame"],
                [12, "failed"]
            ]);
            resolve({
                scene,
                status: timedOut ? "timeout" : (result ? result.status : (exitCode === 0 ? "passed" : (exitStatuses.get(exitCode) || "crash-or-error"))),
                exitCode,
                signal,
                result,
                output
            });
        });
    });
}

const options = parseArguments(process.argv.slice(2));
options.playground = path.resolve(options.playground);
options.bundleDir = path.resolve(options.bundleDir);
if (!existsSync(options.playground)) {
    throw new Error(`Playground executable not found: ${options.playground}`);
}
if (!existsSync(options.bundleDir)) {
    throw new Error(`Bundle directory not found: ${options.bundleDir}`);
}

const availableScenes = readdirSync(options.bundleDir)
    .filter((name) => /^scene\d+\.playground\.js$/.test(name))
    .map((name) => name.slice(0, -".playground.js".length))
    .sort((left, right) => Number(left.slice(5)) - Number(right.slice(5)));
const scenes = options.scenes ? options.scenes.map(normalizeSceneName) : availableScenes;
const results = [];

for (const scene of scenes) {
    if (!availableScenes.includes(scene)) {
        throw new Error(`Bundle not found for ${scene}.`);
    }
    process.stdout.write(`[Babylon-Lite] ${scene} ... `);
    const result = await runScene(options, scene);
    results.push(result);
    console.log(`${result.status}${result.exitCode === null ? "" : ` (exit ${result.exitCode})`}`);
    if (result.status !== "passed") {
        const resultDetails = [
            ...(result.result?.consoleErrors || []),
            result.result?.error
        ].filter(Boolean);
        const detail = resultDetails.length > 0 ? resultDetails.join("\n") : result.output;
        if (detail) {
            console.log(detail.trim().split("\n").slice(-20).join("\n"));
        }
    }
}

const summary = {
    total: results.length,
    passed: results.filter((result) => result.status === "passed").length,
    failed: results.filter((result) => result.status !== "passed").length,
    statuses: Object.fromEntries([...new Set(results.map((result) => result.status))].map((status) => [status, results.filter((result) => result.status === status).length])),
    results
};
console.log(`BABYLON_LITE_NATIVE_SUMMARY ${JSON.stringify({ total: summary.total, passed: summary.passed, failed: summary.failed, statuses: summary.statuses })}`);

if (options.report) {
    const reportPath = path.resolve(options.report);
    writeFileSync(reportPath, `${JSON.stringify(summary, null, 2)}\n`);
    console.log(`Wrote ${reportPath}`);
}
if (summary.failed > 0) {
    process.exitCode = 1;
}
