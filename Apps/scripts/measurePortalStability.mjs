// Captures the Hill Valley stability poses with the macOS Playground and reports
// frame-to-frame instability. Scripts are loaded from this checkout, so shader
// edits need no rebuild. Example:
//   node Apps/scripts/measurePortalStability.mjs \
//     --playground build/Apps/Playground/Playground.app/Contents/MacOS/Playground \
//     --out build/stability --label candidate
import { spawnSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { analyzeCaptures, formatTable, readManifest } from "./analyzePortalStability.mjs";

const scriptsDirectory = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../Playground/Scripts");

// The accepted non-AR AO workload, with the stability capture in place of the moving benchmark.
const chain = [
    "native_xr_portal_hillvalley_stress.js",
    "native_xr_portal_ao_projection_validation.js",
    "native_xr_portal_profile_workload.js",
    "native_xr_portal_gpu_comparison.js",
    "native_xr_portal_gtvbao_cosine_validation.js",
    "native_xr_portal_f16_material_validation.js",
    "native_xr_portal_world_normals_validation.js",
    "native_xr_portal_ao_r8_validation.js",
    "native_xr_portal_color_reuse_validation.js",
    "native_xr_portal_resolution_validation.js",
    "native_xr_portal_ao_multibounce.js",
    "native_xr_portal_stability_capture.js",
    "native_xr_babylon_portal_demo.js",
];

export function parseArguments(argv) {
    const options = { views: ["final", "ao"], modes: ["whole-pixel"], size: "2560x1440", hdr: true, preludes: [], aoScript: chain[4], timeoutSeconds: 600 };
    for (let i = 0; i < argv.length; i++) {
        const name = argv[i];
        const value = () => {
            if (i + 1 >= argv.length) throw new Error(name + " requires a value");
            return argv[++i];
        };
        if (name === "--playground") options.playground = path.resolve(value());
        else if (name === "--out") options.out = path.resolve(value());
        else if (name === "--label") options.label = value();
        else if (name === "--views") options.views = value().split(",");
        else if (name === "--modes") options.modes = value().split(",");
        else if (name === "--size") options.size = value();
        else if (name === "--sdr") options.hdr = false;
        else if (name === "--prelude") options.preludes.push(path.resolve(value()));
        else if (name === "--ao-script") options.aoScript = path.resolve(value());
        else if (name === "--timeout") options.timeoutSeconds = Number(value());
        else throw new Error("Unknown argument: " + name);
    }
    if (!options.playground || !options.out || !/^[a-z0-9-]+$/.test(options.label || "")) {
        throw new Error("Usage: measurePortalStability.mjs --playground <executable> --out <directory> --label <lowercase-name> " +
            "[--views final,ao] [--modes whole-pixel,sub-pixel] [--size WxH] [--sdr] [--prelude <script>] [--ao-script <script>]");
    }
    if (!/^[1-9][0-9]*x[1-9][0-9]*$/.test(options.size) || !(options.timeoutSeconds > 0) ||
        options.views.some((view) => view !== "final" && view !== "ao") ||
        options.modes.some((mode) => mode !== "whole-pixel" && mode !== "sub-pixel")) {
        throw new Error("Invalid --views, --modes, --size or --timeout value.");
    }
    return options;
}

export function buildScriptList(options, configurationPath) {
    const toUrl = (file) => pathToFileURL(path.isAbsolute(file) ? file : path.join(scriptsDirectory, file)).href;
    // Preludes run first so they can configure globals the chain reads while loading.
    return [configurationPath, ...options.preludes, ...chain.map((name, index) => (index === 4 ? options.aoScript : name))].map(toUrl);
}

function capture(options, view, mode) {
    // TestUtils writes next to the build's Apps directory on macOS.
    const captureDirectory = path.resolve(options.playground, "../../../../..");
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), "portal-stability-"));
    const configurationPath = path.join(temporary, "configuration.js");
    fs.writeFileSync(configurationPath, `globalThis.__nativeArPortalStability = ${JSON.stringify({ view, mode, label: options.label })};\n`);
    const prefix = ["stability", options.label, view, mode].join("-");
    for (const name of fs.readdirSync(captureDirectory)) {
        if (name.startsWith(prefix + "-") && name.endsWith(".png")) fs.rmSync(path.join(captureDirectory, name));
    }
    const run = spawnSync(options.playground, [...(options.hdr ? ["--hdr10"] : []), ...buildScriptList(options, configurationPath)], {
        env: { ...process.env, BABYLON_NATIVE_PLAYGROUND_PROFILE_RENDER_SIZE: options.size },
        encoding: "utf8", timeout: options.timeoutSeconds * 1000, maxBuffer: 256 * 1024 * 1024,
    });
    fs.rmSync(temporary, { recursive: true });
    const log = (run.stdout || "") + (run.stderr || "");
    const logPath = path.join(options.out, prefix + ".log");
    fs.writeFileSync(logPath, log);
    if (run.status !== 0 || !log.includes("stability:complete") || /Validation Error|runner:error/.test(log)) {
        throw new Error(`Capture ${prefix} failed (status ${run.status}, signal ${run.signal}); see ${logPath}`);
    }
    for (const name of fs.readdirSync(captureDirectory)) {
        if (name.startsWith(prefix + "-") && name.endsWith(".png")) fs.renameSync(path.join(captureDirectory, name), path.join(options.out, name));
    }
    const manifest = readManifest(log);
    const result = analyzeCaptures(options.out, manifest);
    fs.writeFileSync(path.join(options.out, prefix + ".json"), JSON.stringify({ manifest, result }, null, 1));
    return formatTable(manifest, result);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
    let options;
    try {
        options = parseArguments(process.argv.slice(2));
    } catch (error) {
        console.error(error.message);
        process.exit(2);
    }
    fs.mkdirSync(options.out, { recursive: true });
    for (const view of options.views) {
        for (const mode of options.modes) {
            console.log(capture(options, view, mode));
        }
    }
}
