// Longer non-AR orbit for external CPU/GPU traces. Load before the moving benchmark.
globalThis.__nativeArPortalBenchmarkDurationMs = 180000;
console.log("moving-benchmark:profile-prelude=" + globalThis.__nativeArPortalBenchmarkDurationMs);
