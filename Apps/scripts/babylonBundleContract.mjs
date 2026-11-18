import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

export const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
export const patchRoot = join(repoRoot, "Patches/BabylonJS");
export const bundleFiles = ["babylon.js", "babylon.max.js", "babylon.js.map"];
export const receiptName = "babylon.native-build.json";
export const sha256 = data => createHash("sha256").update(data).digest("hex");
export const fileHash = path => sha256(readFileSync(path));
export const readJson = path => JSON.parse(readFileSync(path, "utf8"));

export function inputs() {
    const profile = readJson(join(patchRoot, "bundle-profile.json"));
    const patches = readFileSync(join(patchRoot, "series"), "utf8").split(/\r?\n/).map(line => line.trim()).filter(line => line && !line.startsWith("#"));
    if (!patches.includes("0002-accessor-inlining.patch")) throw new Error("Accessor patch is missing from the maintained series.");
    const hashes = Object.fromEntries(patches.map(name => [name, fileHash(join(patchRoot, name))]));
    return { profile, patches, hashes, fingerprint: sha256(JSON.stringify({ profile, hashes })) };
}

export function verifyBundle(directory, expectedVariant = "optimized", packaged = false) {
    if (!["control", "optimized"].includes(expectedVariant)) throw new Error("Unknown bundle variant.");
    const { profile, fingerprint } = inputs();
    const receipt = readJson(join(directory, receiptName));
    if (receipt.schema !== 1 || receipt.fingerprint !== fingerprint || receipt.revision !== profile.revision || receipt.variant !== expectedVariant) {
        throw new Error("Bundle receipt does not match the current source, patches or requested variant.");
    }
    for (const name of packaged ? ["babylon.max.js"] : bundleFiles) {
        if (receipt.files?.[name] !== fileHash(join(directory, name))) throw new Error(`Bundle hash mismatch: ${name}. Rebuild; npm may have restored the published package.`);
    }
    const reportPath = join(directory, "babylon.accessor-report.json");
    if (receipt.reportHash !== fileHash(reportPath)) throw new Error("Accessor report hash mismatch.");
    const report = readJson(reportPath);
    if (expectedVariant === "optimized") {
        if (report.mode !== profile.accessorMode || report.includeStatic !== profile.accessorStatic || !(report.total >= profile.minimumRewrites) || !(report.externalPackages?.filesScanned > 0)) {
            throw new Error("Accessor inlining or the external-package override audit did not run.");
        }
        for (const property of profile.requiredProperties) {
            if (!(report.perProp?.[property] > 0)) throw new Error(`Required accessor was not inlined: ${property}`);
        }
    } else if (report.total !== 0) {
        throw new Error("Control bundle must not contain accessor transformations.");
    }
    if (receipt.minifyPasses !== (expectedVariant === "optimized" ? profile.minifyPasses : 2)) throw new Error("Incorrect compression pass count.");
    const require = createRequire(import.meta.url);
    const file = resolve(directory, "babylon.max.js");
    delete require.cache[require.resolve(file)];
    const babylon = require(file);
    if (babylon.Engine.Version !== profile.version) throw new Error("Incorrect Babylon.js version.");
    const plane = babylon.Plane.prototype.dotCoordinate.toString();
    if (expectedVariant === "optimized" && !/\._x/.test(plane)) throw new Error("Packaged code still uses untransformed vector accessors.");
    if (expectedVariant === "control" && /\._x/.test(plane)) throw new Error("Control code contains accessor inlining.");
    return receipt;
}
