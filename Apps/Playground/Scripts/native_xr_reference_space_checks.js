// Test-only prelude for an application that opens an immersive session.
// Cases adapted from WPT webxr/xrSession_requestReferenceSpace{,_features}.https.html.
(function () {
    const xr = navigator.xr;
    const request = xr.requestSession;
    async function check(session, options) {
        let passed = 0;
        let failed = 0;
        async function test(name, body) {
            try {
                if (!(await body())) {
                    throw new Error("unexpected result");
                }
                passed++;
            } catch (error) {
                failed++;
                console.error("XR reference-space FAIL: " + name + ": " + error);
            }
        }
        const requested = new Set([...(options.requiredFeatures || []), ...(options.optionalFeatures || [])]);
        for (const type of ["viewer", "local"]) {
            await test(type + " default", async () => (await session.requestReferenceSpace(type)) instanceof XRReferenceSpace);
        }
        if (requested.has("local-floor") && session.enabledFeatures.includes("local-floor")) {
            await test("granted local-floor", async () => (await session.requestReferenceSpace("local-floor")) instanceof XRReferenceSpace);
        }
        for (const type of ["local-floor", "unbounded"]) {
            if (!requested.has(type)) {
                await test("unrequested " + type, () => session.requestReferenceSpace(type).then(() => false,
                    (error) => error.name === "NotSupportedError"));
            }
        }
        await test("invalid enum", () => session.requestReferenceSpace("not-a-reference-space").then(() => false,
            (error) => error instanceof TypeError));
        await test("convert the reference-space type once", async () => {
            let conversions = 0;
            const space = await session.requestReferenceSpace({ toString() { conversions++; return "local"; } });
            return conversions === 1 && space instanceof XRReferenceSpace;
        });
        await test("preserve conversion exception", () => {
            const failure = new Error("reference space conversion failed");
            return session.requestReferenceSpace({ toString() { throw failure; } }).then(() => false, (error) => error === failure);
        });
        console.log("XR reference-space results: " + passed + " passed, " + failed + " failed");
    }
    Object.defineProperty(xr, "requestSession", { configurable: true, value: async function (mode, options) {
        const session = await request.apply(this, arguments);
        if (mode === "immersive-vr" || mode === "immersive-ar") {
            await check(session, options || {});
        }
        return session;
    } });
})();
