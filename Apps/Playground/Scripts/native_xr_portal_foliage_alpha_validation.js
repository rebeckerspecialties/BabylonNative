// Validation only: restore the source StandardMaterial's cutout semantics for
// foliage. Glass stays blended. The GLB converter incorrectly used BLEND for
// every diffuse alpha texture, even when useAlphaFromDiffuseTexture was false.
(function () {
    var names = ["plan_tree_05", "plan_tree_02", "arbres_plan_04", "plan_tree_03", "buisson", "arbuste",
        "gd_arbre_01_SycamoreLeaves5", "pt_arbre_01_AlderBranches", "pt_arbre_branche_02", "gd_arbre_02_SycamoreLeaves5", "brousaille"];
    var previous = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, status) {
        if (previous) previous(scene, status);
        var materials = names.map(function (name) {
            var matches = scene.materials.filter(function (material) { return material.name === name; });
            if (matches.length !== 1 || matches[0].alpha !== 1 || !matches[0].albedoTexture ||
                !matches[0].albedoTexture.hasAlpha || matches[0].transparencyMode !== BABYLON.Material.MATERIAL_ALPHABLEND) {
                throw new Error("Foliage alpha comparison requires the original blended GLB material: " + name);
            }
            return matches[0];
        });
        materials.forEach(function (material) {
            material.transparencyMode = BABYLON.Material.MATERIAL_ALPHATEST;
            material.alphaCutOff = 0.4;
        });
        status("shimmer-diagnostic:foliage-alpha-test=" + materials.length + ":cutoff=0.4:glass-unchanged");
    };
})();
