const fs = require("node:fs");
const path = require("node:path");
const { execFileSync } = require("node:child_process");
const [archive, output, mode = "timing"] = process.argv.slice(2);
if (!["timing", "pixels"].includes(mode)) throw Error("Unknown qualification mode");
if (!archive || !output) throw Error("Usage: node buildMinimal.cjs babylonjs-9.37.1.tgz OUTFILE");
const unpack = (name) => execFileSync("tar", ["-xzOf", archive, "package/" + name], { encoding: "utf8", maxBuffer: 64 * 1024 * 1024 });
if (JSON.parse(unpack("package.json")).version !== "9.37.1") throw Error("Expected Babylon.js 9.37.1");
let bundle = unpack("babylon.max.js");
bundle = require("./parentBuild.cjs").withParentClear(bundle);
// Mirror the relevant corrections in the interactive bundle. Fail closed
// rather than silently timing a smaller eye or retaining the old attachment.
for (const [before, after] of [
    // Request sampled usage explicitly for the post-timing pixel test.
    [
        "i=e.createProjectionLayer(t),n=new ptt",
        "i=e.createProjectionLayer({...t,textureUsage:t.textureUsage|GPUTextureUsage.TEXTURE_BINDING,scaleFactor:this._options.projectionLayerInit?.scaleFactor??t.scaleFactor}),n=new ptt",
    ],
    ["n&&(a._depthStencilTexture=n),s.disableRescaling()", "n&&a.setDepthStencilTexture(n),s.disableRescaling()"],
    [
        'this._getPipeline("rgba8unorm"),this._getVideoPipeline("rgba8unorm")',
        'this._getPipeline("rgba8unorm"),typeof this._device.importExternalTexture==="function"&&this._getVideoPipeline("rgba8unorm")',
    ],
    [
        "t.skipInitialClear||e.isRightCamera||",
        "t.skipInitialClear||(e.isRightCamera&&!(" + require("./eyeClear.cjs").needsSeparateEyeClear + ")(e.rigParent?.rigCameras[0]?.outputRenderTarget,t))||",
    ],
]) {
    if (bundle.split(before).length !== 2) throw Error("Unexpected upstream XR code: " + before);
    bundle = bundle.replace(before, after);
}
const local = (name) => fs.readFileSync(path.join(__dirname, name), "utf8");
fs.writeFileSync(output, [bundle, `const questPixelsOnly=${mode === "pixels"};`, local("stages.js"), local("minimal.mjs")].join("\n;\n"));
