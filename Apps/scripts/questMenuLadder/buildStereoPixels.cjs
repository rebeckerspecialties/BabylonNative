// Qualification-only Babylon WebGPU build. The normal renderer and browser capability flags are unchanged.
const fs = require("node:fs");
const path = require("node:path");
const [workspace, modules, output, mode] = process.argv.slice(2);
if (!["mono-passes", "multiview"].includes(mode)) throw Error("Expected WORKSPACE NODE_MODULES OUTPUT mono-passes|multiview");
const core = path.resolve(modules, "@babylonjs/core");
if (require(path.join(core, "package.json")).version !== "9.37.1") throw Error("Expected Babylon.js 9.37.1");
const mv = mode === "multiview";
const read = file => fs.readFileSync(path.join(__dirname, file), "utf8");
const changes = new Map();
function change(file, before, after) {
    let src = changes.get(file) ?? fs.readFileSync(path.join(core, file), "utf8");
    if (src.split(before).length !== 2) throw Error("Expected exactly one match in " + file + ": " + before);
    changes.set(file, src.replace(before, () => after));
}
const xr = "XR/features/WebXRLayers.pure.js";
if (mv) {
    const compiled = process.argv[6];
    if (!compiled) throw Error("Expected the compiled core/dist directory from the patched 9.37.1 source checkout");
    const files = [
        "Engines/Extensions/engine.multiview.pure.js", "Engines/WebGPU/webgpuCacheRenderPipeline.js",
        "Engines/WebGPU/webgpuShaderProcessorsWGSL.pure.js", "Engines/webgpuEngine.pure.js",
        "ShadersWGSL/default.vertex.js", "ShadersWGSL/pbr.vertex.js", "XR/features/Layers/WebXRWebGPUCompositionLayer.js",
        xr, "XR/webXRLayerRenderTargetTexture.js", "XR/webXRWebGPURenderTargetTextureProvider.js", "scene.pure.js"
    ];
    for (const file of files) changes.set(file, fs.readFileSync(path.join(compiled, file), "utf8"));
}
change(xr, "binding.createProjectionLayer(init)", "binding.createProjectionLayer({ ...init, scaleFactor: 2048/1216, textureUsage: init.textureUsage | GPUTextureUsage.TEXTURE_BINDING })");
const entries = [
    ["WebGPUEngine", "Engines/webgpuEngine"], ["Scene", "scene"], ["FreeCamera", "Cameras/freeCamera"],
    ["Matrix, Vector3", "Maths/math.vector"], ["Color3, Color4", "Maths/math.color"], ["StandardMaterial", "Materials/standardMaterial"],
    ["DynamicTexture", "Materials/Textures/dynamicTexture"], ["MeshBuilder", "Meshes/meshBuilder"],
    ["WebXRExperienceHelper", "XR/webXRExperienceHelper"], ["WebXRFeatureName", "XR/webXRFeaturesManager"]
];
const imports = "import {" + entries.map(e => e[0]).join(",") + '} from "@babylonjs/core";';
const esbuild = require(require.resolve("esbuild", { paths: [workspace] }));
esbuild.build({
    stdin: { contents: imports + '\nimport "@babylonjs/core/XR/features/WebXRLayers.js";\nimport "@babylonjs/core/Engines/Extensions/engine.multiview.js";\nconst BABYLON = {' + entries.map(e => e[0]).join(",") + "};\nconst questPixelsOnly = true; const expectedStereo = " + mv + ";\n" + read("stages.js") + "\n" + read("minimal.mjs"), resolveDir: __dirname },
    outfile: output, platform: "browser", format: "iife", bundle: true, minify: true, target: "chrome112",
    alias: { "@babylonjs/core": core, core },
    plugins: [{ name: "xr-qualification", setup(build) {
        build.onLoad({ filter: /\.js$/ }, args => {
            const contents = changes.get(path.relative(core, args.path));
            if (contents !== undefined) return {contents, loader: "js", resolveDir: path.dirname(args.path)};
        });
    } }]
}).catch(() => process.exitCode = 1);
