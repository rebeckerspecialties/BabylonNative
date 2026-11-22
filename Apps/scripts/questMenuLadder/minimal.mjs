async function runMinimal() {
    const {
        WebGPUEngine,
        Scene,
        FreeCamera,
        Vector3,
        Color3,
        Color4,
        StandardMaterial,
        DynamicTexture,
        WebXRExperienceHelper,
        WebXRFeatureName,
        MeshBuilder,
    } = BABYLON;
    navigator.gpu._installRenderPassCommandRecorderProvider?.(WebGPUEngine);
    const canvas = document.createElement("canvas");
    const engine = new WebGPUEngine(canvas, { xrCompatible: true, enableAllFeatures: true, audioEngine: false });
    await engine.initAsync();
    const errors = new Set();
    engine._device.addEventListener("uncapturederror", ({ error }) => {
        if (errors.size < 8 && !errors.has(error.message)) {
            errors.add(error.message);
            console.error("Menu GPU validation", error.message);
        }
    });
    const scene = new Scene(engine);
    scene.activeCamera = new FreeCamera("viewer", new Vector3(0, 1.6, 0), scene);
    scene.clearColor = new Color4(0.07, 0.11, 0.18, 1);
    const xr = await WebXRExperienceHelper.CreateAsync(scene);
    xr.camera.skipUnusedParentFramebufferClear = true;
    xr.featuresManager.enableFeature(WebXRFeatureName.LAYERS, "latest", {
        preferMultiviewOnInit: typeof expectedStereo !== "undefined" && expectedStereo,
        projectionLayerInit: { scaleFactor: 2048 / 1216 },
    });
    engine.runRenderLoop(() => scene.render());
    await xr.enterXRAsync("immersive-vr", "local-floor", xr.sessionManager.getWebXRRenderTarget(), { requiredFeatures: ["webgpu"] });
    await xr.sessionManager.updateTargetFrameRate(72);
    xr.sessionManager.onXRFrameObservable.addOnce((frame) => {
        const manager = xr.sessionManager,
            layer = manager._getBaseLayerWrapper().layer;
        const binding = manager._getGraphicsBinding().binding;
        const subs = frame.getViewerPose(manager.referenceSpace).views.map((v) => binding.getViewSubImage(layer, v));
        console.info(
            "Menu projection layout",
            JSON.stringify({
                layers: layer.textureArrayLength,
                shared: subs[0].colorTexture === subs[1].colorTexture,
                slices: subs.map((sub) => sub.getViewDescriptor().baseArrayLayer),
                quad: typeof binding.createQuadLayer === "function",
                cube: typeof binding.createCubeLayer === "function",
            }),
        );
    });
    function solid(name, color) {
        const material = new StandardMaterial(name, scene);
        material.disableLighting = true;
        material.emissiveColor = color;
        return material;
    }
    const box = MeshBuilder.CreateBox("near parallax marker", { size: 0.28 }, scene);
    box.parent = xr.camera;
    box.position.set(-0.38, 0, 1.6);
    box.material = solid("orange", new Color3(0.9, 0.35, 0.04));
    const wall = MeshBuilder.CreateBox("opaque room", { size: 15, sideOrientation: 1 }, scene);
    wall.material = solid("room", new Color3(0.28, 0.46, 0.52));
    const panel = MeshBuilder.CreatePlane("static label", { width: 1.3, height: 0.325, sideOrientation: 2 }, scene);
    panel.parent = xr.camera;
    panel.position.set(0.12, 0, 3);
    const texture = new DynamicTexture("static menu text", { width: 1024, height: 256 }, scene, false);
    texture.drawText("Stereo menu", null, 156, "bold 88px sans-serif", "#ffffff", "#123152", true, true);
    panel.material = solid("label", Color3.White());
    panel.material.emissiveTexture = texture;
    await measureQuestMenuStages(
        scene,
        questPixelsOnly ? ["clear"] : ["clear", "marker", "room+marker", "room+marker+label", "clear-again"],
        (stage) => {
            box.setEnabled(!stage.startsWith("clear"));
            wall.setEnabled(stage.startsWith("room"));
            panel.setEnabled(stage.endsWith("label"));
            return { meshes: scene.meshes.filter((mesh) => mesh.isEnabled()).map((mesh) => mesh.name) };
        },
    );
    // Check the eye images after all timing windows; no readback during a sample.
    box.setEnabled(true);
    const projected = [];
    const device = engine._device;
    const module = device.createShaderModule({
        code: `
        @group(0) @binding(0) var leftEye: texture_2d<f32>;
        @group(0) @binding(1) var rightEye: texture_2d<f32>;
        @group(0) @binding(2) var<storage,read_write> outPixels: array<vec2u>;
        @compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3u) {
            let uv = (vec2f(id.xy) + 0.5) / vec2f(128, 16);
            let pos = vec2i(uv * vec2f(textureDimensions(leftEye)));
            outPixels[id.y*128u+id.x] = vec2u(pack4x8unorm(textureLoad(leftEye,pos,0)),pack4x8unorm(textureLoad(rightEye,pos,0)));
        }`,
    });
    const pipeline = device.createComputePipeline({ layout: "auto", compute: { module, entryPoint: "main" } });
    const size = 128 * 16 * 8;
    const out = device.createBuffer({ size, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
    const read = device.createBuffer({ size, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    await scene.whenReadyAsync();
    await new Promise((resolve) => setTimeout(resolve, 1000));
    try {
        await new Promise((resolve, reject) =>
            engine.onEndFrameObservable.addOnce(() => {
                try {
                    const multiview = xr.camera._renderingMultiview === true;
                    if (typeof expectedStereo !== "undefined" && expectedStereo !== multiview) throw Error("Unexpected camera multiview state: " + multiview);
                    const eyes = xr.camera.rigCameras.map((c, eyeIndex) => {
                        const target = multiview ? xr.camera.outputRenderTarget : c.outputRenderTarget,
                            viewport = c.viewport.toGlobal(128, 16);
                        const p = Vector3.Project(
                            box.getBoundingInfo().boundingBox.centerWorld,
                            BABYLON.Matrix.IdentityReadOnly,
                            c.getTransformationMatrix(),
                            viewport,
                        );
                        projected.push([p.x - 0.5, p.y - 0.5]);
                        return { texture: target?.getInternalTexture()?._hardwareTexture?.underlyingResource, slice: multiview ? eyeIndex : target?.layerIndex };
                    });
                    if (eyes.length !== 2 || !eyes[0].texture || eyes[0].texture !== eyes[1].texture ||
                        eyes[0].texture.depthOrArrayLayers !== 2 || eyes.some((eye, i) => eye.slice !== i))
                        throw Error("Expected two ordered slices of the same acquired projection texture");
                    const group = device.createBindGroup({
                        layout: pipeline.getBindGroupLayout(0),
                        entries: [
                            ...eyes.map(({ texture, slice }, binding) => ({
                                binding, resource: texture.createView({ dimension: "2d", baseArrayLayer: slice, arrayLayerCount: 1 }),
                            })),
                            { binding: 2, resource: { buffer: out } },
                        ],
                    });
                    const enc = device.createCommandEncoder(),
                        pass = enc.beginComputePass();
                    pass.setPipeline(pipeline);
                    pass.setBindGroup(0, group);
                    pass.dispatchWorkgroups(2, 16);
                    pass.end();
                    enc.copyBufferToBuffer(out, 0, read, 0, size);
                    device.queue.submit([enc.finish()]);
                    resolve();
                } catch (e) {
                    reject(e);
                }
            }),
        );
        await read.mapAsync(GPUMapMode.READ);
        const words = new Uint32Array(read.getMappedRange());
        // Preserve every sampled pixel in eye order, not just the dominant colors or centroids.
        const runs = [0, 1].map((eye) => {
            const result = [];
            for (let i = eye; i < words.length; i += 2) {
                const last = result[result.length - 1];
                if (last && last[0] === words[i]) last[1]++;
                else result.push([words[i], 1]);
            }
            return result;
        });
        console.info("Menu stereo readback", JSON.stringify(runs));
        const colors = [0, 1].map((eye) => {
            const histogram = new Map();
            for (let i = eye; i < words.length; i += 2) histogram.set(words[i], (histogram.get(words[i]) ?? 0) + 1);
            return [...histogram].sort((a, b) => b[1] - a[1]).slice(0, 4);
        });
        console.info("Menu stereo colors", JSON.stringify({ colors, projected, draws: engine.countersLastFrame, active: scene.getActiveMeshes().length }));
        const centers = [0, 1].map((eye) => {
            let n = 0,
                x = 0,
                y = 0;
            for (let i = eye; i < words.length; i += 2) {
                const rgb = words[i],
                    r = rgb & 255,
                    g = (rgb >>> 8) & 255,
                    b = (rgb >>> 16) & 255;
                if (r > 100 && r > g * 1.4 && r > b * 2) {
                    ++n;
                    x += (i >> 1) % 128;
                    y += Math.floor((i >> 1) / 128);
                }
            }
            if (!n) {
                throw Error("Missing orange marker in eye " + eye + ", projected=" + JSON.stringify(projected) +
                    ", dominant pixels=" + JSON.stringify(colors[eye]));
            }
            return [x / n, y / n];
        });
        console.info("Menu stereo pixels", JSON.stringify({ centers, projected }));
        if (Math.abs(centers[0][0] - centers[1][0]) < 0.25) throw Error("Marker has no measurable stereo disparity");
        for (let eye = 0; eye < 2; eye++) {
            if (
                !projected[eye].every(Number.isFinite) ||
                Math.abs(centers[eye][0] - projected[eye][0]) > 2 ||
                Math.abs(centers[eye][1] - projected[eye][1]) > 1
            )
                throw Error("Eye " + eye + " marker disagrees with rig projection");
        }
        console.info("Menu stereo marker PASS", JSON.stringify({ multiview: xr.camera._renderingMultiview === true }));
    } finally {
        if (read.mapState === "mapped") read.unmap();
        read.destroy();
        out.destroy();
    }
}
runMinimal().catch((e) => console.error("Menu ladder FAIL", String(e), e?.stack));
