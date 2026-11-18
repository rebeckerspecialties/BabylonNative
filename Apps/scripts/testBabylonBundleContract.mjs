import assert from "node:assert/strict";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import { bundleFiles, fileHash, inputs, receiptName, verifyBundle } from "./babylonBundleContract.mjs";

function fixture(t, variant = "optimized") {
    const directory = mkdtempSync(join(tmpdir(), "babylon-receipt-"));
    t.after(() => rmSync(directory, { recursive: true, force: true }));
    const { profile, fingerprint } = inputs();
    const code = `module.exports = { Engine: { Version: "${profile.version}" }, Plane: class { dotCoordinate(v) { return v.${variant === "optimized" ? "_x" : "x"}; } } };`;
    for (const name of bundleFiles) writeFileSync(join(directory, name), code);
    const report = variant === "optimized" ? {
        mode: profile.accessorMode, includeStatic: profile.accessorStatic, total: profile.minimumRewrites,
        externalPackages: { filesScanned: 1 },
        perProp: Object.fromEntries(profile.requiredProperties.map(name => [name, 1])),
    } : { mode: "off", total: 0 };
    writeFileSync(join(directory, "babylon.accessor-report.json"), JSON.stringify(report));
    const receipt = { schema: 1, fingerprint, revision: profile.revision, variant,
        minifyPasses: variant === "optimized" ? profile.minifyPasses : 2,
        files: Object.fromEntries(bundleFiles.map(name => [name, fileHash(join(directory, name))])),
        reportHash: fileHash(join(directory, "babylon.accessor-report.json")),
    };
    const save = () => writeFileSync(join(directory, receiptName), JSON.stringify(receipt));
    save();
    return { directory, receipt, report, save };
}

test("verified optimized and explicitly requested control packages pass", t => {
    for (const variant of ["optimized", "control"]) {
        assert.equal(verifyBundle(fixture(t, variant).directory, variant).variant, variant);
    }
});

test("control cannot silently satisfy an optimized build", t => {
    assert.throws(() => verifyBundle(fixture(t, "control").directory), /requested variant/);
});

test("packaged verification checks the executed bundle and report without requiring unused source maps", t => {
    const f = fixture(t);
    rmSync(join(f.directory, "babylon.js"));
    rmSync(join(f.directory, "babylon.js.map"));
    assert.equal(verifyBundle(f.directory, "optimized", true).variant, "optimized");
    assert.throws(() => verifyBundle(f.directory), /ENOENT/);
    writeFileSync(join(f.directory, "babylon.max.js"), "stale packaged script");
    assert.throws(() => verifyBundle(f.directory, "optimized", true), /hash mismatch/);
});

test("missing receipt and npm-replaced bundle are hard failures", t => {
    const f = fixture(t);
    writeFileSync(join(f.directory, "babylon.max.js"), "published replacement");
    assert.throws(() => verifyBundle(f.directory), /hash mismatch/);
    rmSync(join(f.directory, receiptName));
    assert.throws(() => verifyBundle(f.directory), /ENOENT/);
});

test("changed patch fingerprint, source revision and compression options fail", t => {
    for (const [key, value] of [["fingerprint", "old"], ["revision", "old"], ["minifyPasses", 1]]) {
        const f = fixture(t);
        f.receipt[key] = value;
        f.save();
        assert.throws(() => verifyBundle(f.directory));
    }
});

test("no-op transformer, missing required sites or missing override audit fail", t => {
    for (const change of [report => { report.total = 0; }, report => { delete report.perProp["Vector3.x"]; }, report => { delete report.perProp["UniformBuffer.isSync"]; }, report => { report.includeStatic = !report.includeStatic; }, report => { report.externalPackages.filesScanned = 0; }]) {
        const f = fixture(t);
        change(f.report);
        writeFileSync(join(f.directory, "babylon.accessor-report.json"), JSON.stringify(f.report));
        f.receipt.reportHash = fileHash(join(f.directory, "babylon.accessor-report.json"));
        f.save();
        assert.throws(() => verifyBundle(f.directory));
    }
});

test("receipt alone cannot disguise untransformed executable code", t => {
    const f = fixture(t);
    writeFileSync(join(f.directory, "babylon.max.js"), 'module.exports={Engine:{Version:"9.36.0"},Plane:class {dotCoordinate(v){return v.x;}}};');
    f.receipt.files["babylon.max.js"] = fileHash(join(f.directory, "babylon.max.js"));
    f.save();
    assert.throws(() => verifyBundle(f.directory), /untransformed/);
});
