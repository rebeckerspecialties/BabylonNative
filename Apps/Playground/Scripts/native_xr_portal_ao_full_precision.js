// Validation only: evaluates each AO sample's slab and slice angles in 32-bit floats instead of 16-bit ones,
// for devices without shader-f16 and for comparisons.
// Load before native_xr_portal_gtvbao_cosine_validation.js.
globalThis.__nativeArPortalAoPrecision = "full";
