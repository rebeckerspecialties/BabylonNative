// Qualification only. Report after settling and bracket the window in logcat
// so compositor samples and asynchronous GPU timestamps can be joined by PID.
async function measureQuestMenuStages(scene, stages, apply) {
    const delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
    const stats = () => navigator.gpu._backendStats();
    const keys = [
        "drawCallCount",
        "renderPassBeginCount",
        "queueSubmitCount",
        "renderPipelineCreateCount",
        "canvasGpuSnapshotCount",
        "canvasCpuSnapshotCount",
    ];
    const engine = scene.getEngine();
    for (const stage of stages) {
        const details = await apply(stage);
        await delay(4000);
        const active = scene.activeCamera;
        const views = active?.rigCameras?.map((camera) => [camera.outputRenderTarget?.getSize(), camera.viewport]);
        const before = stats(),
            start = performance.now();
        let frames = 0;
        const handle = scene.onAfterRenderObservable.add(() => ++frames);
        console.info(
            "Menu ladder begin",
            JSON.stringify({ stage, details, views, version: engine.constructor.Version, multiview: !!engine.getCaps().multiview }),
        );
        await delay(16000);
        const elapsed = performance.now() - start,
            after = stats();
        scene.onAfterRenderObservable.remove(handle);
        console.info(
            "Menu ladder end",
            JSON.stringify({ stage, frames, elapsed, counters: Object.fromEntries(keys.map((k) => [k, after[k] - before[k]])) }),
        );
    }
    console.info("Menu ladder complete");
}
