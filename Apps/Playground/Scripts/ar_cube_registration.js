// Prelude for ar_cube.js: log, every XR frame, where the scene camera projects a fixed world
// point. Launch with BABYLON_NATIVE_XR_TRACE_REGISTRATION=1 so the host logs ARKit's own
// projection of the same point, then compare the two streams.
globalThis.__arCubeLogRegistration = true;
