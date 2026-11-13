/// Scratch benchmark harness (not for upstream). Mode comes from globalThis.BENCH_MODE.
(function () {
    const MODE = (globalThis.BENCH_MODE || "render").split(":")[0];
    const out = (tag, obj) => console.log("BENCH " + tag + " " + JSON.stringify(obj));
    const now = () => performance.now();
    const VARIANT = String(globalThis.BENCH_VARIANT || (globalThis.BENCH_MODE || '').split(':')[1] || '');
    // ---- JS self-time breakdown (scratch): wraps Babylon.js methods with exclusive-time timers.
    function benchProfile(scene, engine, frameCounter, outFn) {
        const pnow = () => performance.now();
        const acc = new Map(); const stack = [];
        const wrap = (obj, name, label) => {
            if (!obj) return; const f = obj[name]; if (typeof f !== "function" || f.__benchWrapped) return;
            const e = { self: 0, calls: 0 }; acc.set(label, e);
            const w = function () {
                const fr = { child: 0 }; stack.push(fr); const t = pnow();
                try { return f.apply(this, arguments); }
                finally { const d = pnow() - t; stack.pop(); e.self += d - fr.child; e.calls++; if (stack.length) stack[stack.length - 1].child += d; }
            };
            w.__benchWrapped = true; w.__orig = f; obj[name] = w; undo.push(() => { obj[name] = f; });
        };
        const undo = [];
        const B = BABYLON, P = (c) => c && c.prototype;
        for (const n of ["render", "_evaluateActiveMeshes", "_renderForCamera", "animate", "_processSubCameras", "_checkIsReady", "updateTransformMatrix"]) wrap(P(B.Scene), n, "Scene." + n);
        for (const n of ["render", "_renderOpaqueSorted", "_renderAlphaTestSorted", "_renderTransparentSorted"]) wrap(P(B.RenderingGroup), n, "RenderingGroup." + n);
        wrap(P(B.RenderingManager), "render", "RenderingManager.render");
        for (const n of ["render", "_bind", "_draw", "_processRendering", "_renderWithInstances", "_renderWithThinInstances", "_preActivate", "_activate", "_getRenderingMesh", "getLOD"]) wrap(P(B.Mesh), n, "Mesh." + n);
        for (const n of ["computeWorldMatrix", "isInFrustum", "_updateBoundingInfo", "_updateSubMeshesBoundingInfo", "isCompletelyInFrustum"]) wrap(P(B.AbstractMesh), n, "AbstractMesh." + n);
        wrap(P(B.TransformNode), "computeWorldMatrix", "TransformNode.computeWorldMatrix");
        for (const n of ["render", "isInFrustum", "updateBoundingInfo", "getMaterial", "_getDrawWrapper"]) wrap(P(B.SubMesh), n, "SubMesh." + n);
        for (const n of ["isReadyForSubMesh", "bindForSubMesh"]) wrap(P(B.StandardMaterial), n, "StandardMaterial." + n);
        for (const n of ["_preBind", "_afterBind", "bindOnlyWorldMatrix", "bindView", "bindViewProjection", "bindEyePosition"]) wrap(P(B.Material), n, "Material." + n);
        for (const n of ["update", "updateMatrix", "updateColor4", "updateFloat4", "bindToEffect", "bindUniformBuffer"]) wrap(P(B.UniformBuffer), n, "UniformBuffer." + n);
        for (const n of ["getMeshUniformBuffer"]) wrap(P(B.AbstractMesh), n, "AbstractMesh." + n);
        for (const n of ["drawElementsType", "drawArraysType", "_draw", "bindBuffers", "enableEffect", "endFrame", "beginFrame", "_flushRenderPassCommands", "setViewport", "clear", "setState", "applyStates", "_endCurrentRenderPass", "_startMainRenderPass", "flushFramebuffer", "setDepthStencilState", "_setState", "bindUniformBufferBase", "updateUniformBuffer", "updateDynamicVertexBuffer", "_applyViewport", "_applyScissor", "_commitFramebuffer", "setMatrices", "setFloat3", "setFloat4", "setTexture", "_bindTextureDirectly", "bindSamplers", "wipeCaches", "restoreDefaultFramebuffer", "_viewport", "submitCommands", "_updateAnisotropicLevel", "setZOffset", "setAlphaMode", "setDepthWrite", "setDepthBuffer"]) wrap(engine, n, "Engine." + n);
        for (const [o, n, l] of [[engine._cacheRenderPipeline, "getRenderPipeline", "WebGPUCacheRenderPipeline.getRenderPipeline"], [engine._cacheBindGroups, "getBindGroups", "WebGPUCacheBindGroups.getBindGroups"], [engine._renderPassDrawBatcher, "flush", "RenderPassDrawBatcher.flush"], [engine._renderPassDrawBatcher, "enqueue", "RenderPassDrawBatcher.enqueue"], [engine._renderPassCommandLowering, "record", "CommandLowering.record"], [engine._uniformsBuffers, "x", "x"], [engine._bufferManager, "setSubData", "WebGPUBufferManager.setSubData"], [engine._ubInvertY, "update", "x2"]]) wrap(o, n, l);
        for (const o of [engine._renderPassDrawBatcher, engine._renderPassCommandLowering, engine._commandBufferEncoder]) {
            if (!o) continue; let p = Object.getPrototypeOf(o);
            for (const n of Object.getOwnPropertyNames(p)) if (n !== "constructor" && typeof o[n] === "function") wrap(o, n, (o === engine._commandBufferEncoder ? "NativeCommandBufferEncoder." : o === engine._renderPassDrawBatcher ? "RenderPassDrawBatcher." : "CommandLowering.") + n);
        }
        // calibrate wrapper overhead with an empty wrapped function
        const cal = { f() { } }; wrap(cal, "f", "__cal"); const tc = pnow(); for (let i = 0; i < 20000; i++) cal.f(); const perCall = (pnow() - tc) / 20000; acc.delete("__cal");
        const FR = 180;
        return new Promise((resolve) => {
            const f0 = frameCounter(), t0 = pnow();
            const iv = setInterval(() => {
                const n = frameCounter() - f0;
                if (n < FR) return;
                clearInterval(iv); const wall = pnow() - t0; for (const u of undo) u();
                let calls = 0, selfSum = 0; const rows = [];
                for (const [k, e] of acc) { if (!e.calls) continue; calls += e.calls; selfSum += e.self; rows.push([k, +(e.self / n).toFixed(3), +(e.calls / n).toFixed(1), +((e.self - e.calls * perCall * 0.5) / n).toFixed(3)]); }
                rows.sort((a, b) => b[1] - a[1]);
                outFn("js-breakdown", { frames: n, instrumentedFrameMs: +(wall / n).toFixed(2), wrappedCallsPerFrame: +(calls / n).toFixed(0), wrapperUsPerCall: +(perCall * 1000).toFixed(2), estWrapperMsPerFrame: +(calls / n * perCall).toFixed(2), selfMsPerFrameTotal: +(selfSum / n).toFixed(2), "rows[name,selfMs/frame,calls/frame,selfMinusHalfWrapper]": rows.slice(0, 34) });
                resolve();
            }, 250);
        });
    }
    function benchApplyVariant(variant, scene, engine, mats) {
        const v = String(variant || "");
        if (v.includes("frozen")) { scene.freezeActiveMeshes(); for (const m of mats) m.freeze(); scene.skipPointerMovePicking = true; scene.autoClearDepthAndStencil = true; }
        if (v.includes("noncompat")) { engine.compatibilityMode = false; }
        if (v.includes("snapshot")) { engine.snapshotRenderingMode = BABYLON.Constants.SNAPSHOTRENDERING_FAST; engine.snapshotRendering = true; }
        if (v.includes("worldfrozen")) { for (const m of scene.meshes) m.freezeWorldMatrix && 0; }
    }

    function stats(a) {
        const s = Float64Array.from(a).sort();
        const q = (p) => s[Math.min(s.length - 1, Math.floor(p * s.length))];
        let sum = 0; for (const v of s) sum += v;
        const mean = sum / s.length;
        let sq = 0; for (const v of s) sq += (v - mean) * (v - mean);
        const med = q(0.5);
        let h15 = 0, h2 = 0, over33 = 0;
        for (const v of s) { if (v > med * 1.5) h15++; if (v > med * 2) h2++; if (v > 33.4) over33++; }
        return {
            n: s.length, fps: +(1000 / mean).toFixed(2), mean: +mean.toFixed(3), sd: +Math.sqrt(sq / s.length).toFixed(3),
            p50: +med.toFixed(3), p90: +q(0.9).toFixed(3), p99: +q(0.99).toFixed(3), max: +s[s.length - 1].toFixed(3),
            gt1_5xMedian: h15, gt2xMedian: h2, gt33ms: over33,
        };
    }

    out("start", { mode: MODE, wasm: typeof WebAssembly, fetch: typeof fetch, ua: (globalThis.navigator && navigator.userAgent) || null });

    const engine = new BABYLON.NativeEngine();
    const scene = new BABYLON.Scene(engine);
    scene.createDefaultCamera(true, true, true);
    scene.activeCamera.position = new BABYLON.Vector3(0, 0, -30);
    scene.activeCamera.setTarget(BABYLON.Vector3.Zero());
    new BABYLON.HemisphericLight("l", new BABYLON.Vector3(0, 1, 0), scene);

    // 15x15 individually drawn spheres with a handful of materials: enough JS + command-stream
    // work per frame to be measurable on an A12 without being GPU bound.
    const mats = [];
    for (let i = 0; i < 8; i++) {
        const m = new BABYLON.StandardMaterial("m" + i, scene);
        m.diffuseColor = new BABYLON.Color3((i & 1), ((i >> 1) & 1), ((i >> 2) & 1) * 0.8 + 0.2);
        mats.push(m);
    }
    const root = new BABYLON.TransformNode("root", scene);
    const src = BABYLON.MeshBuilder.CreateSphere("s", { segments: 12, diameter: 1 }, scene);
    src.setEnabled(false);
    const N = 15; const instSrc = [];
    for (let x = 0; x < N; x++) for (let y = 0; y < N; y++) {
        const mi = (x * 7 + y) % mats.length;
            let c;
            if (VARIANT.includes("instances")) {
                if (!instSrc[mi]) { instSrc[mi] = src.clone("src" + mi); instSrc[mi].material = mats[mi]; instSrc[mi].parent = root; instSrc[mi].setEnabled(true); c = instSrc[mi]; }
                else c = instSrc[mi].createInstance("i" + x + "_" + y);
            } else c = src.clone("c" + x + "_" + y);
        c.setEnabled(true);
        c.parent = root;
        c.position.set((x - N / 2) * 1.4, (y - N / 2) * 1.4, 0);
        if (!VARIANT.includes('instances')) c.material = mats[mi];
    }

    const frames = []; let frameCount = 0;
    let last = 0, perFrame = null, measuring = false;
    engine.runRenderLoop(function () {
        const t = now();
        if (measuring && last) frames.push(t - last);
        last = t; frameCount++;
        root.rotation.y += 0.01;
        if (perFrame) perFrame();
        scene.render();
    });

    function measure(tag, seconds, extra) {
        return new Promise((resolve) => {
            frames.length = 0; last = 0; measuring = true;
            setTimeout(() => {
                measuring = false;
                const r = stats(frames);
                // Pacing: distribution of frame intervals bucketed to the 60 Hz grid.
                const b = { le12: 0, v16: 0, v33: 0, v50: 0, gt58: 0 };
                for (const f of frames) { if (f < 12) b.le12++; else if (f < 25) b.v16++; else if (f < 41.7) b.v33++; else if (f < 58.4) b.v50++; else b.gt58++; }
                out(tag, Object.assign(r, { buckets: b }, extra ? extra() : {}));
                resolve(r);
            }, seconds * 1000);
        });
    }
    const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

    // ---------- modes ----------
    async function modeRender() {
        await sleep(4000);
        out("mark", { phase: "render-begin", t: now() });
        benchApplyVariant(VARIANT, scene, engine, mats);
        await sleep(1500);
        await measure("render", VARIANT ? 20 : 45, () => ({ variant: VARIANT, activeMeshes: scene.getActiveMeshes().length }));
        await benchProfile(scene, engine, () => frameCount, out);
        // timer accuracy while rendering: setTimeout(0) and setTimeout(4) latency
        const lat0 = [], lat4 = [];
        for (let i = 0; i < 200; i++) { const t = now(); await new Promise((r) => setTimeout(r, 0)); lat0.push(now() - t); }
        for (let i = 0; i < 200; i++) { const t = now(); await new Promise((r) => setTimeout(r, 4)); lat4.push(now() - t - 4); }
        out("timers", { setTimeout0: stats(lat0), setTimeout4_overshoot: stats(lat4) });
        // microtask / promise turnaround
        const pl = [];
        for (let i = 0; i < 2000; i++) { const t = now(); await null; pl.push(now() - t); }
        out("microtask", stats(pl));
    }

    async function modeIdle() {
        // No rendering at all: measures dispatch-loop idle cost (wakeups) for the power trace.
        engine.stopRenderLoop();
        out("mark", { phase: "idle-begin", t: now() });
        let ticks = 0;
        const id = setInterval(() => { ticks++; }, 1000);
        await sleep(45000);
        clearInterval(id);
        out("idle", { ticks });
    }

    async function modeGc() {
        await sleep(4000);
        // Garbage churn: ~1.5 MB of short-lived objects per frame, plus retained ring to promote some.
        const ring = new Array(256); let ri = 0;
        perFrame = () => {
            for (let i = 0; i < 300; i++) {
                const o = { a: new Float32Array(1024), b: [i, i + 1, i + 2], s: "x" + i };
                if ((i & 31) === 0) ring[ri++ & 255] = o;
            }
        };
        out("mark", { phase: "gc-begin", t: now() });
        await measure("gc-churn", 40);
        perFrame = null;
        // then go quiet: with run-loop GC timers the heap should be collected while idle.
        await measure("gc-after", 15);
    }

    async function modeTexture() {
        await sleep(4000);
        const mk = (i) => new BABYLON.RawTexture(new Uint8Array(64 * 64 * 4).fill(i & 255), 64, 64, BABYLON.Constants.TEXTUREFORMAT_RGBA, scene, false, false);
        // (a) per-frame churn: create 16, dispose the 16 from two frames ago.
        const q = []; let k = 0; let disposeMs = 0, disposeN = 0;
        perFrame = () => {
            const batch = []; for (let i = 0; i < 16; i++) batch.push(mk(k++));
            q.push(batch);
            if (q.length > 2) { const old = q.shift(); const t = now(); for (const tx of old) tx.dispose(); disposeMs += now() - t; disposeN += old.length; }
        };
        out("mark", { phase: "texture-churn-begin", t: now() });
        await measure("texture-churn", 30, () => ({ usPerDispose: +(disposeMs * 1000 / disposeN).toFixed(2), disposed: disposeN }));
        perFrame = null;
        for (const b of q) for (const tx of b) tx.dispose();
        await sleep(1000);
        // (b) bulk: 2000 textures created, rendered a few frames, disposed in one task. Repeat 5x.
        const bulk = [];
        for (let rep = 0; rep < 5; rep++) {
            const txs = []; for (let i = 0; i < 2000; i++) txs.push(mk(i));
            await sleep(500);
            const t = now(); for (const tx of txs) tx.dispose(); bulk.push(+(now() - t).toFixed(3));
            await sleep(500);
        }
        out("texture-bulk-dispose-2000-ms", { runs: bulk });
        // (c) dispose interleaved with queued commands in the same task (the ordering case #2071 fixes):
        // update a texture then dispose another, 2000 times.
        const keep = mk(1); const data = new Uint8Array(64 * 64 * 4);
        const inter = [];
        for (let rep = 0; rep < 5; rep++) {
            const txs = []; for (let i = 0; i < 2000; i++) txs.push(mk(i));
            await sleep(500);
            const t = now();
            for (const tx of txs) { keep.update(data); tx.dispose(); }
            inter.push(+(now() - t).toFixed(3));
            await sleep(500);
        }
        out("texture-interleaved-update-dispose-2000-ms", { runs: inter });
        await measure("texture-after", 5);
    }

    async function modeWasm() {
        await sleep(3000);
        const r = { typeofWebAssembly: typeof WebAssembly };
        if (typeof WebAssembly === "object") {
            // (module (func (export "add") (param i32 i32) (result i32) local.get 0 local.get 1 i32.add))
            const bytes = new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0, 1, 7, 1, 96, 2, 127, 127, 1, 127, 3, 2, 1, 0, 7, 7, 1, 3, 97, 100, 100, 0, 0, 10, 9, 1, 7, 0, 32, 0, 32, 1, 106, 11]);
            try { const t = now(); const m = new WebAssembly.Module(bytes); const i = new WebAssembly.Instance(m); r.sync = { add: i.exports.add(2, 3), ms: +(now() - t).toFixed(3) }; } catch (e) { r.syncError = String(e); }
            for (const name of ["compile", "instantiate"]) {
                const t = now();
                try {
                    const v = await Promise.race([WebAssembly[name](bytes), sleep(8000).then(() => "TIMEOUT")]);
                    r[name] = { ms: +(now() - t).toFixed(3), result: v === "TIMEOUT" ? v : Object.prototype.toString.call(v) };
                } catch (e) { r[name] = { error: String(e) }; }
            }
        }
        out("wasm", r);
        await measure("wasm-render", 10);
    }

    function xhr(url, opts) {
        opts = opts || {};
        return new Promise((resolve) => {
            const x = new XMLHttpRequest();
            const t0 = now(); const ev = [];
            const done = (kind) => {
                let all = null, ct = null, ctUpper = null, missing;
                try { all = x.getAllResponseHeaders(); } catch (e) { all = "THROW " + e; }
                try { ct = x.getResponseHeader("content-type"); ctUpper = x.getResponseHeader("CONTENT-TYPE"); missing = x.getResponseHeader("x-not-there"); } catch (e) { ct = "THROW " + e; }
                let len = -1; try { len = x.responseType === "arraybuffer" ? x.response.byteLength : x.responseText.length; } catch (e) { }
                resolve({
                    url, kind, ms: +(now() - t0).toFixed(1), status: x.status, statusText: x.statusText, readyState: x.readyState,
                    responseURL: x.responseURL, len, ct, ctUpper, missing: missing === undefined ? "undefined" : missing,
                    events: ev.join(","), headers: opts.headers ? all : undefined,
                    headerLines: typeof all === "string" ? all.split("\r\n").filter(Boolean).length : -1,
                    altSvc: (() => { try { return x.getResponseHeader("alt-svc"); } catch (e) { return "THROW"; } })(),
                });
            };
            for (const e of ["loadstart", "progress", "load", "error", "abort", "timeout", "loadend", "readystatechange"]) {
                x.addEventListener(e, () => { ev.push(e === "readystatechange" ? "rs" + x.readyState : e); if (e === "loadend") done("loadend"); });
            }
            try {
                x.open(opts.method || "GET", url, true);
                if (opts.responseType) x.responseType = opts.responseType;
                x.send();
                if (opts.abortAfter !== undefined) setTimeout(() => x.abort(), opts.abortAfter);
            } catch (e) { resolve({ url, kind: "throw", error: String(e) }); }
            setTimeout(() => resolve({ url, kind: "harness-timeout", events: ev.join(","), status: x.status, readyState: x.readyState }), 20000);
        });
    }

    async function modeNet() {
        await sleep(3000);
        const bust = "?b=" + Date.now().toString(36);
        const origins = [
            ["h2-only", "https://raw.githubusercontent.com/BabylonJS/Babylon.js/master/.gitignore"],
            ["h2-only", "https://assets.babylonjs.com/environments/environmentSpecular.env"],
            ["h3-no-dns-rr", "https://cdn.jsdelivr.net/npm/babylonjs@7.0.0/package.json"],
            ["h3-no-dns-rr", "https://quic.nginx.org/"],
            ["h3-dns-rr", "https://www.cloudflare.com/robots.txt"],
            ["h3-dns-rr", "https://www.google.com/robots.txt"],
        ];
        // 1. cold first contact, sequential, then two repeats on the now-known origin.
        for (const [cls, u] of origins) {
            for (let i = 0; i < 3; i++) {
                const r = await xhr(u + bust + i, { responseType: "arraybuffer" });
                out("net-seq", { cls, i, r });
                if (i === 0) await sleep(1500); // let Alt-Svc settle
            }
        }
        // 2. multi-stream: 16 parallel requests per origin; frame pacing measured while they run.
        for (const [cls, u] of [origins[1], origins[2], origins[4]]) {
            const t = now();
            const m = measure("net-par-frames", 3);
            const rs = await Promise.all(Array.from({ length: 16 }, (_, i) => xhr(u + bust + "p" + i, { responseType: "arraybuffer" })));
            out("net-par", { cls, url: u, wallMs: +(now() - t).toFixed(1), ok: rs.filter((r) => r.status === 200).length, ms: rs.map((r) => r.ms) });
            await m;
        }
        // 3. conformance probes.
        out("conf-headers", await xhr("https://cdn.jsdelivr.net/npm/babylonjs@7.0.0/package.json" + bust + "h", { headers: true }));
        out("conf-headers-h2", await xhr("https://raw.githubusercontent.com/BabylonJS/Babylon.js/master/.gitignore" + bust + "h", { headers: true }));
        out("conf-404", await xhr("https://cdn.jsdelivr.net/npm/babylonjs@7.0.0/nope-not-here.json" + bust));
        out("conf-redirect", await xhr("https://github.com/BabylonJS/Babylon.js/raw/master/.gitignore" + bust));
        out("conf-http-plain", await xhr("http://neverssl.com/" + bust));
        out("conf-dns-fail", await xhr("https://no-such-host.invalid/x"));
        out("conf-abort", await xhr("https://assets.babylonjs.com/environments/environmentSpecular.env" + bust + "a", { abortAfter: 5 }));
        out("conf-app", await xhr("app:///Scripts/config.json"));
        out("conf-app-404", await xhr("app:///Scripts/nope.json"));
        if (typeof fetch === "function") {
            try {
                const t = now(); const res = await fetch("https://cdn.jsdelivr.net/npm/babylonjs@7.0.0/package.json" + bust + "f");
                const hs = []; if (res.headers && res.headers.forEach) res.headers.forEach((v, k) => hs.push(k));
                out("conf-fetch", { ms: +(now() - t).toFixed(1), status: res.status, statusText: res.statusText, ok: res.ok, url: res.url, redirected: res.redirected, type: res.type, headerNames: hs.slice(0, 40) });
            } catch (e) { out("conf-fetch", { error: String(e) }); }
        }
    }

    const modes = { render: modeRender, idle: modeIdle, gc: modeGc, texture: modeTexture, wasm: modeWasm, net: modeNet };
    scene.executeWhenReady(() => {
        (modes[MODE] || modeRender)().then(
            () => out("done", { mode: MODE }),
            (e) => out("done", { mode: MODE, error: String(e && e.stack || e) }));
    });
})();
