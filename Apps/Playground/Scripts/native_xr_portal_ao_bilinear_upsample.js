// Validation only: keep the published bilinear AO read in the combine pass.
// Load before native_xr_portal_gtvbao_cosine_validation.js. This removes nine
// texture loads per screen pixel and lets AO spread across silhouettes again.
globalThis.__nativeArPortalAoUpsample = "bilinear";
