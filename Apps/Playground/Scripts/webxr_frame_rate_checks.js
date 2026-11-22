// Run against a real immersive session. Leave valid rate selection to the application.
async function checkXRFrameRateControl(session) {
    const rates = session.supportedFrameRates;
    if (!rates) {
        if (session.frameRate != null) throw new Error("Unexpected nominal rate without display rate control");
        return;
    }
    if (!(rates instanceof Float32Array) || !rates.length || !(session.frameRate > 0)) {
        throw new Error("Invalid XR display rates: " + rates + ", nominal=" + session.frameRate);
    }
    let rejected = false;
    try {
        await session.updateTargetFrameRate(-1);
    } catch (error) {
        if (error.name !== "TypeError") throw error;
        rejected = true;
    }
    if (!rejected) throw new Error("Unsupported XR display rate accepted");
    console.info("XR rate control", Array.from(rates).join(","), "nominal", session.frameRate);
}
