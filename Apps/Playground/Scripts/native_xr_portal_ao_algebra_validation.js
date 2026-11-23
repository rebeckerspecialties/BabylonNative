// Validation only: retain both orthogonal slices with one trigonometric pair.
(function () {
    var store = BABYLON.ShaderStore.ShadersStoreWGSL;
    var source = store.ssao2PixelShader;
    function replaceOnce(pattern, replacement, name) {
        if (typeof source !== "string" || (source.match(new RegExp(pattern.source, "g")) || []).length !== 1) {
            throw new Error("AO algebra validation no longer matches " + name);
        }
        source = source.replace(pattern, replacement);
    }
    replaceOnce(/const PORTAL_SLICE_COUNT: u32 = 2u;/, "$&\n// portalOrthogonalSlices", "two-slice configuration");
    replaceOnce(/let normalLength = length\(normal\);\s*if \(normalLength < 1e-6\)/,
        "let normalSquared = dot(normal, normal);\n    if (normalSquared < 1e-12)", "normal guard");
    replaceOnce(/normal \/= normalLength;/, "normal *= inverseSqrt(normalSquared);", "normalization");
    replaceOnce(/var occluded = 0u;/, `let phi = noise.x * (3.14159265359 / 2.0);
    let rotation = vec2f(cos(phi), sin(phi));
    var occluded = 0u;`, "slice rotation");
    replaceOnce(/let phi = \(f32\(slice\) \+ noise.x\) \* \(3.14159265359 \/ f32\(PORTAL_SLICE_COUNT\)\);\s*let tangent = basisX \* cos\(phi\) \+ basisY \* sin\(phi\);/,
        `let direction = select(rotation, vec2f(-rotation.y, rotation.x), slice == 1u);
        let tangent = basisX * direction.x + basisY * direction.y;`, "orthogonal tangent");
    // Install atomically so source drift cannot leave a partial shader patch.
    store.ssao2PixelShader = source;
    globalThis.__nativeArPortalBenchmarkSuffix = (globalThis.__nativeArPortalBenchmarkSuffix || "") + "-ao-algebra";
})();
