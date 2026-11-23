// Only for a dedicated projection-only benchmark with no mirror consumer.
function isUnusedParentClear(scene, camera) {
    if (
        !camera ||
        camera !== scene.activeCamera ||
        camera.rigCameras?.length !== 2 ||
        camera.outputRenderTarget ||
        camera._multiviewTexture ||
        scene.prePass ||
        !scene.getEngine()._currentFrameBufferIsDefaultFrameBuffer()
    )
        return false;
    const [l, r] = camera.rigCameras.map((c) => c.outputRenderTarget?.getInternalTexture()?._hardwareTexture?.underlyingResource);
    return !!l && !!r && l !== r;
}
