import { readFileSync, writeFileSync } from "node:fs";

// Replace only the inspection fixture's autoplay, not application source.
function runWelcomeControls(scene) {
    const visible = new Map(scene.meshes.map((mesh) => [mesh, mesh.isVisible]));
    const sky = (mesh) => /skybox/i.test(mesh.name);
    const panel = (mesh) => /AdvancedDynamicTextureMaterial|backingMeshMaterial/i.test(mesh.material?.name ?? "");
    return measureQuestMenuStages(scene, ["full-first", "empty", "sky", "sky+panels", "panels", "without-sky", "full-last"], (stage) => {
        let visibleMeshes = 0;
        for (const [mesh, original] of visible) {
            const show =
                stage.startsWith("full") ||
                (stage === "sky" && sky(mesh)) ||
                (stage === "sky+panels" && (sky(mesh) || panel(mesh))) ||
                (stage === "panels" && panel(mesh)) ||
                (stage === "without-sky" && !sky(mesh));
            mesh.isVisible = original && show;
            if (mesh.isVisible && mesh.isEnabled()) ++visibleMeshes;
        }
        return { visibleMeshes, totalMeshes: scene.meshes.length };
    }).finally(() => {
        for (const [mesh, original] of visible) mesh.isVisible = original;
    });
}

function runParentClearControls(scene) {
    let skip = false,
        skipped = 0;
    const clear = scene._clearFrameBuffer;
    scene._clearFrameBuffer = function (camera) {
        if (skip && isUnusedParentClear(scene, camera)) {
            ++skipped;
            return;
        }
        return clear.call(this, camera);
    };
    return measureQuestMenuStages(scene, ["parent-clear", "skip-parent", "parent-restored", "skip-repeat", "parent-final"], (stage) => {
        const lastSkipped = skipped;
        skip = stage.startsWith("skip");
        skipped = 0;
        return { lastSkipped };
    }).finally(() => {
        scene._clearFrameBuffer = clear;
    });
}

const [input, output, mode = "meshes"] = process.argv.slice(2);
if (!["meshes", "parent", "pixels"].includes(mode)) throw Error("Unknown menu control");
const src = readFileSync(input, "utf8");
const hook = 'setTimeout(()=>{console.info("Factotum education autoplay");g()},30000)';
if (src.split(hook).length !== 2) throw Error("Expected exactly one inspection autoplay boundary");
const common = readFileSync(new URL("stages.js", import.meta.url), "utf8");
const parent = readFileSync(new URL("parent.js", import.meta.url), "utf8");
if (mode === "pixels") {
    const pixels = readFileSync(new URL("menuPixels.js", import.meta.url), "utf8");
    const init = "e.createProjectionLayer({...t,scaleFactor:this._options.projectionLayerInit?.scaleFactor??t.scaleFactor})";
    if (src.split(init).length !== 2) throw Error("Expected exactly one projection-layer constructor");
    const sampled = src.replace(init, init.replace("...t,", "...t,textureUsage:t.textureUsage|GPUTextureUsage.TEXTURE_BINDING,"));
    writeFileSync(
        output,
        [
            parent,
            pixels,
            sampled.replace(hook, 'setTimeout(()=>compareMenuEyes(_).catch(e=>console.error("Menu ladder FAIL",String(e),e.stack)),4000)'),
        ].join("\n"),
    );
    process.exit(0);
}
const run = mode === "parent" ? runParentClearControls : runWelcomeControls;
const start = `setTimeout(()=>${run.name}(_).catch(e=>console.error("Menu ladder FAIL",String(e),e.stack)),1000)`;
writeFileSync(output, [common, parent, run.toString(), src.replace(hook, start)].join("\n"));
