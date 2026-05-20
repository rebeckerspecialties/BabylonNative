#!/usr/bin/env node

const childProcess = require("child_process");
const fs = require("fs");
const os = require("os");
const path = require("path");

// Builds the packaged ASTC cubemap used for portal reflections from a loose
// Hill Valley source-asset tree. The Playground package keeps only the KTX
// output, so regeneration must be pointed at an unpacked source directory.

function fail(message) {
    console.error(message);
    process.exit(1);
}

function parseArgs(argv) {
    const args = {
        sourceDir: path.resolve(__dirname, "../Playground/Scripts/native_xr_portal_assets"),
        outputDir: path.resolve(__dirname, "../Playground/Scripts/native_xr_portal_assets"),
        basisu: process.env.BASISU || "basisu",
        astcBlock: "8x8",
        effort: "6",
    };

    for (let index = 2; index < argv.length; index++) {
        const arg = argv[index];
        const next = argv[index + 1];
        if (arg === "--source-dir" && next) {
            args.sourceDir = path.resolve(next);
            index++;
        } else if (arg === "--output-dir" && next) {
            args.outputDir = path.resolve(next);
            index++;
        } else if (arg === "--basisu" && next) {
            args.basisu = next;
            index++;
        } else if (arg === "--astc-block" && next) {
            args.astcBlock = next;
            index++;
        } else if (arg === "--effort" && next) {
            args.effort = next;
            index++;
        } else {
            fail("Usage: generateNativeXrPortalEnvironment.js [--source-dir dir] [--output-dir dir] [--basisu path] [--astc-block 8x8] [--effort 6]");
        }
    }

    return args;
}

function run(command, args, options = {}) {
    childProcess.execFileSync(command, args, {
        cwd: options.cwd,
        env: process.env,
        stdio: process.env.BASISU_VERBOSE === "1" ? "inherit" : (options.stdio || "pipe"),
    });
}

function generateCubeKtx(args) {
    const tempRoot = fs.mkdtempSync(path.join(os.tmpdir(), "bn-hv-env-"));
    const faceFiles = [
        ["native_xr_portal_cubmap_hhv_px.jpg", "px"],
        ["native_xr_portal_cubmap_hhv_nx.jpg", "nx"],
        ["native_xr_portal_cubmap_hhv_py.jpg", "py"],
        ["native_xr_portal_cubmap_hhv_ny.jpg", "ny"],
        ["native_xr_portal_cubmap_hhv_pz.jpg", "pz"],
        ["native_xr_portal_cubmap_hhv_nz.jpg", "nz"],
    ];

    try {
        const inputFiles = [];
        for (const [fileName, token] of faceFiles) {
            const sourcePath = path.join(args.sourceDir, fileName);
            if (!fs.existsSync(sourcePath)) {
                throw new Error(`Source cubemap face '${sourcePath}' was not found. The packaged Playground keeps only optimized ASTC assets; pass --source-dir with a local Hill Valley loose-asset tree.`);
            }
            const outputPath = path.join(tempRoot, `native_xr_portal_environment_${token}.jpg`);
            fs.copyFileSync(sourcePath, outputPath);
            inputFiles.push(outputPath);
        }

        run(args.basisu, [
            "-ktx2",
            "-ktx2_no_zstandard",
            "-cubemap",
            `-astc_ldr_${args.astcBlock}`,
            "-effort",
            String(args.effort),
            "-mipmap",
            "-output_file",
            "native_xr_portal_environment.ktx2",
            ...inputFiles,
        ], { cwd: tempRoot });

        run(args.basisu, ["-unpack", "native_xr_portal_environment.ktx2", "-ktx_only"], { cwd: tempRoot });

        const blockToken = `ASTC_LDR_${args.astcBlock.toUpperCase().replace("X", "x").replace("x", "X")}_RGBA`;
        let candidates = fs.readdirSync(tempRoot).filter((file) => file.endsWith(".ktx") && file.includes(blockToken) && file.includes("_cubemap_"));
        if (candidates.length === 0) {
            candidates = fs.readdirSync(tempRoot).filter((file) => file.endsWith(".ktx") && file.includes(blockToken));
        }
        if (candidates.length !== 1) {
            throw new Error(`Expected one ${blockToken} cube KTX candidate, found ${candidates.length}.`);
        }

        fs.mkdirSync(args.outputDir, { recursive: true });
        fs.copyFileSync(path.join(tempRoot, candidates[0]), path.join(args.outputDir, "native_xr_portal_environment.ktx"));
    } finally {
        fs.rmSync(tempRoot, { recursive: true, force: true });
    }
}

const args = parseArgs(process.argv);
generateCubeKtx(args);
console.log(`Wrote ${path.join(args.outputDir, "native_xr_portal_environment.ktx")}`);
