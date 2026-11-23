import { readFileSync, writeFileSync } from "node:fs";
import { pathToFileURL } from "node:url";

// Install only in a disposable, dated replay bundle, never in the application.
export function cubeCheckpoint(scene) {
    const modes = ["baseline", "surface", "fixed-frame", "surface", "unlit", "hidden", "surface", "baseline"];
    let mesh, material, plugin, edges, initial, stage = -1, since = 0, frameMask = 0, observer, animated;
    return () => {
        if (stage === modes.length) return;
        const onDate = new Date(globalThis.__questPlaybackDate || 0).toISOString().slice(0, 10) === "2016-01-10";
        if (!onDate && stage < 0) return;
        if (!mesh) {
            mesh = scene.getMeshByName("baseMesh-default");
            material = mesh?.material;
            edges = mesh?.edgesRenderer;
            plugin = material?.pluginManager?.getPlugin("Emissive");
            if (!mesh?.thinInstanceCount || !edges?.isEnabled || !plugin?.isEnabled || !mesh.isEnabled() || material.disableLighting) {
                throw Error("Cube replay source, edges, or lit emissive material missing");
            }
            initial = {mask: mesh.layerMask};
            const slices = mesh._userThinInstanceBuffersStorage?.data?.animationFrame;
            if (!slices || slices.length < mesh.thinInstanceCount) throw Error("Missing cube atlas indices");
            animated = slices.subarray(0, mesh.thinInstanceCount).reduce((n,s) => n + (s === 0), 0);
            observer = scene.onBeforeRenderObservable.add(() => { frameMask |= 1 << plugin._frame; });
            since = performance.now();
        }
        let next = Math.min(modes.length, Math.floor((performance.now() - since) / 25000));
        const invalid = !onDate || next > stage + 1;
        if (invalid) next = modes.length;
        if (next === stage) return;
        if (stage >= 0) console.info("Quest cube end", JSON.stringify({stage, mode: modes[stage], count: mesh.thinInstanceCount, animated, frameMask, indices: scene.getActiveIndices()}));
        stage = next;
        const mode = modes[next] || "baseline";
        frameMask = 0;
        plugin.__questCubeFixedFrame = mode === "fixed-frame" ? plugin._frame : undefined;
        mesh.layerMask = mode === "hidden" ? 0 : initial.mask;
        edges.isEnabled = mode === "baseline";
        material.disableLighting = mode === "unlit";
        if (next === modes.length) {
            scene.onBeforeRenderObservable.remove(observer);
            console.info(invalid ? "Quest cube invalid: checkpoint changed or interval skipped" : "Quest area complete");
        } else console.info("Quest cube begin", stage, mode);
    };
}
export function instrument(source) {
    for (const [site, substitute] of [
        ["this._frame=t<12?t:24-t-1", "this._frame=this.__questCubeFixedFrame??(t<12?t:24-t-1)"],
        ["const timer = setInterval(() => {\n        const stats", cubeCheckpoint.toString() + "\nconst cubeTick=cubeCheckpoint(scene);const timer = setInterval(() => {\n        cubeTick();const stats"],
    ]) {
        if (source.split(site).length !== 2) throw Error("Unrecognized cube checkpoint fixture: " + site);
        source = source.replace(site, () => substitute);
    }
    return source;
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const [input, output] = process.argv.slice(2);
    writeFileSync(output, instrument(readFileSync(input, "utf8")));
}
