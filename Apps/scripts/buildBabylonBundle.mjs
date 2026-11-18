import { copyFileSync, existsSync, mkdirSync, rmSync, symlinkSync, writeFileSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { join, resolve } from "node:path";
import { bundleFiles, fileHash, inputs, patchRoot, readJson, receiptName, repoRoot, verifyBundle } from "./babylonBundleContract.mjs";

function run(command, args, cwd, env = {}) {
    const result = spawnSync(command, args, { cwd, env: { ...process.env, ...env }, stdio: "inherit" });
    if (result.status !== 0) throw new Error(`${command} failed (${result.status ?? result.error?.message}).`);
}

const options = { variant: "optimized", source: resolve(repoRoot, "../Babylon.js"), dependencies: undefined, install: false, reuse: false };
try {
    for (let i = 2; i < process.argv.length; i++) {
        const arg = process.argv[i];
        if (arg === "--install") options.install = true;
        else if (arg === "--reuse") options.reuse = true;
        else if (["--variant", "--source", "--dependencies"].includes(arg)) {
            const value = process.argv[++i];
            if (!value || value.startsWith("--")) throw new Error(`Missing value for ${arg}`);
            options[arg.slice(2)] = value;
        } else throw new Error(`Unknown argument ${arg}`);
    }
    if (!["control", "optimized"].includes(options.variant)) throw new Error("Variant must be control or optimized.");
    const { profile, patches, hashes, fingerprint } = inputs();
    const build = join(repoRoot, "build_babylon_bundle");
    const source = join(build, "source");
    const output = join(build, options.variant);
    mkdirSync(build, { recursive: true });
    if (!options.reuse) {
        // Only this script's generated source tree is replaced; never touch the canonical checkout.
        rmSync(source, { recursive: true, force: true });
        mkdirSync(source);
        const archive = join(build, "source.tar");
        run("git", ["archive", "--format=tar", `--output=${archive}`, profile.revision, "package.json", "package-lock.json", "tsconfig.json", "tsconfig.build.json", "tsconfig.devpackages.json", "scripts", "packages/dev", "packages/public", "packages/tools"], options.source);
        run("tar", ["-xf", archive, "-C", source], repoRoot);
        rmSync(archive);
        for (const patch of patches) {
            const args = ["apply", "--whitespace=nowarn", join(patchRoot, patch)];
            const env = { GIT_CEILING_DIRECTORIES: build };
            run("git", ["apply", "--check", "--whitespace=nowarn", join(patchRoot, patch)], source, env);
            run("git", args, source, env);
        }
        const dependencies = resolve(options.dependencies || join(options.source, "node_modules"));
        if (!existsSync(join(dependencies, "typescript/package.json"))) throw new Error(`Missing build dependencies at ${dependencies}; pass --dependencies or install them in Babylon.js.`);
        symlinkSync(dependencies, join(source, "node_modules"), "dir");
        run("npm", ["run", "compile:assets"], join(source, "packages/dev/core"));
        const packageDirectory = join(source, "packages/public/umd/babylonjs");
        const report = join(build, "accessor-report.json");
        rmSync(report, { force: true });
        const minifyPasses = options.variant === "optimized" ? profile.minifyPasses : 2;
        run(process.execPath, [join(dependencies, "rollup/dist/bin/rollup"), "-c", "rollup.config.umd.mjs", "--environment", "ROLLUP_MODE:production"], packageDirectory, {
            BABYLON_ACCESSOR_INLINE: options.variant === "optimized" ? profile.accessorMode : "off",
            BABYLON_ACCESSOR_INLINE_STATIC: options.variant === "optimized" && profile.accessorStatic ? "1" : "0",
            BABYLON_ACCESSOR_INLINE_REPORT: report,
            BABYLON_ACCESSOR_INLINE_DOWNCAST: "site",
            BABYLON_ACCESSOR_INLINE_SIBLINGS: "1",
            BABYLON_ACCESSOR_INLINE_EXTERNAL: "1",
            BABYLON_ACCESSOR_INLINE_MAPPED: "1",
            BABYLON_MINIFY_PASSES: String(minifyPasses),
        });
        mkdirSync(output, { recursive: true });
        for (const name of bundleFiles) copyFileSync(join(packageDirectory, name), join(output, name));
        if (options.variant === "optimized") copyFileSync(report, join(output, "babylon.accessor-report.json"));
        else writeFileSync(join(output, "babylon.accessor-report.json"), JSON.stringify({ mode: "off", total: 0 }));
        writeFileSync(join(output, receiptName), JSON.stringify({
            schema: 1, revision: profile.revision, fingerprint, variant: options.variant, patches: hashes, minifyPasses,
            typescript: readJson(join(dependencies, "typescript/package.json")).version,
            terser: readJson(join(dependencies, "terser/package.json")).version,
            files: Object.fromEntries(bundleFiles.map(name => [name, fileHash(join(output, name))])),
            reportHash: fileHash(join(output, "babylon.accessor-report.json")),
        }, null, 2) + "\n");
    }
    verifyBundle(output, options.variant);
    if (options.install) {
        const target = join(repoRoot, "Apps/node_modules/babylonjs");
        for (const name of [...bundleFiles, "babylon.accessor-report.json", receiptName]) copyFileSync(join(output, name), join(target, name));
        verifyBundle(target, options.variant);
    }
    console.log(`Verified ${options.variant} bundle at ${output}${options.install ? "; installed into Apps" : ""}.`);
} catch (error) {
    console.error(error.stack);
    process.exitCode = 1;
}
