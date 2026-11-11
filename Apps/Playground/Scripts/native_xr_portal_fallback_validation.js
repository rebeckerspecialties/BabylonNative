globalThis.__nativeArPortalAllowFallbackPlacement = true;
globalThis.__nativeArPortalExitDelayMs = 120000;
globalThis.__nativeArPortalLogMeshDiagnostics = true;
// Hosts without a native status sink (the iOS Playground) would otherwise run this
// validation silently; mirror the demo's status stream to the console.
if (typeof globalThis.__nativePlaygroundStatus !== "function") {
    globalThis.__nativePlaygroundStatus = function (message) {
        console.log("[status] " + message);
    };
}
