// Validation only: occlusion inside foliage, computed once from the foliage's
// own geometry and stored as a vertex attribute.
// Bushes and tree crowns are lacework far finer than an AO texel, so the
// screen-space estimate leaves them out and they would shade flat. Each
// alpha-textured, double-sided mesh is voxelized instead; a vertex is darkened
// by how much of its own mesh lies between it and the sky. The result is fixed
// to the mesh, so it cannot flicker. A material plugin multiplies the shaded
// color by it, as the AO combine does for other surfaces.
// Load before native_xr_portal_gtvbao_cosine_validation.js.
(function () {
    var settings = globalThis.__nativeArPortalFoliageOcclusion || {};
    var cellSize = Number(settings.cellSize ?? 0.25);
    var steps = Number(settings.steps ?? 8);
    var cellTransmittance = Number(settings.cellTransmittance ?? 0.7);
    var minimumVisibility = Number(settings.minimumVisibility ?? 0.35);
    if (!(cellSize > 0) || !(steps >= 1) || !(cellTransmittance > 0 && cellTransmittance < 1) || !(minimumVisibility > 0 && minimumVisibility <= 1)) {
        throw new Error("Invalid portal foliage occlusion settings.");
    }
    // The baked term replaces the fixed visibility the AO combine gives cutouts.
    globalThis.__nativeArPortalCutoutAoVisibility = globalThis.__nativeArPortalCutoutAoVisibility ?? 1;
    var maximumCells = 8 << 20;
    // Thirteen directions over the upper hemisphere: light reaches foliage from the sky.
    var directions = [[0, 1, 0]];
    [[15, 0], [50, 30]].forEach(function (ring) {
        var elevation = ring[0] * Math.PI / 180;
        for (var i = 0; i < 6; i++) {
            var azimuth = (ring[1] + i * 60) * Math.PI / 180;
            directions.push([Math.cos(elevation) * Math.cos(azimuth), Math.sin(elevation), Math.cos(elevation) * Math.sin(azimuth)]);
        }
    });

    /**
     * Visibility in [minimumVisibility, 1] of every vertex that a triangle uses.
     * positions are world-space xyz triples. Returns null for a mesh too large to voxelize.
     */
    function bake(positions, indices) {
        var low = [Infinity, Infinity, Infinity];
        var high = [-Infinity, -Infinity, -Infinity];
        for (var n = 0; n < indices.length; n++) {
            for (var axis = 0; axis < 3; axis++) {
                var value = positions[indices[n] * 3 + axis];
                if (value < low[axis]) low[axis] = value;
                if (value > high[axis]) high[axis] = value;
            }
        }
        var size = [0, 1, 2].map(function (axis) { return Math.floor((high[axis] - low[axis]) / cellSize) + 1; });
        if (!(size[0] * size[1] * size[2] <= maximumCells)) return null;
        var sx = size[0], sy = size[1], sz = size[2];
        var occupied = new Uint8Array(sx * sy * sz);
        var inverse = 1 / cellSize;
        var cellOf = function (x, y, z) {
            return Math.floor((x - low[0]) * inverse) + sx * (Math.floor((y - low[1]) * inverse) + sy * Math.floor((z - low[2]) * inverse));
        };
        for (var t = 0; t + 2 < indices.length; t += 3) {
            var a = indices[t] * 3, b = indices[t + 1] * 3, c = indices[t + 2] * 3;
            var longest = 0;
            for (var e = 0; e < 3; e++) {
                longest = Math.max(longest, Math.abs(positions[a + e] - positions[b + e]), Math.abs(positions[b + e] - positions[c + e]), Math.abs(positions[c + e] - positions[a + e]));
            }
            // Sample the triangle at least once per cell it can cross.
            var divisions = Math.min(64, Math.ceil(longest * inverse * 1.5) + 1);
            for (var i = 0; i <= divisions; i++) {
                for (var j = 0; i + j <= divisions; j++) {
                    var u = i / divisions, v = j / divisions, w = 1 - u - v;
                    occupied[cellOf(positions[a] * w + positions[b] * u + positions[c] * v,
                        positions[a + 1] * w + positions[b + 1] * u + positions[c + 1] * v,
                        positions[a + 2] * w + positions[b + 2] * u + positions[c + 2] * v)] = 1;
                }
            }
        }
        // Vertices in one cell share a value, so each occupied cell is marched once.
        var cellVisibility = new Float32Array(occupied.length).fill(-1);
        var visibility = new Float32Array(positions.length / 3).fill(1);
        var used = new Uint8Array(visibility.length);
        var best = 0;
        for (var n2 = 0; n2 < indices.length; n2++) {
            var vertex = indices[n2];
            if (used[vertex]) continue;
            used[vertex] = 1;
            var cx = Math.floor((positions[vertex * 3] - low[0]) * inverse);
            var cy = Math.floor((positions[vertex * 3 + 1] - low[1]) * inverse);
            var cz = Math.floor((positions[vertex * 3 + 2] - low[2]) * inverse);
            var cell = cx + sx * (cy + sy * cz);
            if (cellVisibility[cell] < 0) {
                var sum = 0;
                for (var d = 0; d < directions.length; d++) {
                    var direction = directions[d];
                    var transmittance = 1;
                    var last = cell;
                    // Start beyond the cell's own neighbours, so a flat card does not shade itself.
                    for (var s = 2; s < steps + 2; s++) {
                        var x = Math.floor(cx + 0.5 + direction[0] * s), y = Math.floor(cy + 0.5 + direction[1] * s), z = Math.floor(cz + 0.5 + direction[2] * s);
                        if (x < 0 || y < 0 || z < 0 || x >= sx || y >= sy || z >= sz) break;
                        var next = x + sx * (y + sy * z);
                        if (next !== last && occupied[next]) transmittance *= cellTransmittance;
                        last = next;
                    }
                    sum += transmittance;
                }
                cellVisibility[cell] = sum / directions.length;
            }
            visibility[vertex] = cellVisibility[cell];
            if (visibility[vertex] > best) best = visibility[vertex];
        }
        // The most open vertex of the mesh keeps its full color.
        for (var k = 0; k < visibility.length; k++) {
            visibility[k] = used[k] ? minimumVisibility + (1 - minimumVisibility) * Math.min(1, visibility[k] / best) : 1;
        }
        return visibility;
    }
    globalThis.__nativeArPortalBakeFoliageOcclusion = bake;

    var attribute = "portalFoliageVisibility";
    var pluginName = "PortalFoliageOcclusion";
    class FoliageOcclusionPlugin extends BABYLON.MaterialPluginBase {
        constructor(material) {
            super(material, pluginName, 200, { PORTAL_FOLIAGE_OCCLUSION: false });
            this._enable(true);
        }
        getClassName() { return pluginName + "Plugin"; }
        isCompatible(shaderLanguage) { return shaderLanguage === BABYLON.ShaderLanguage.WGSL; }
        prepareDefines(defines, scene, mesh) { defines.PORTAL_FOLIAGE_OCCLUSION = mesh.isVerticesDataPresent(attribute); }
        getAttributes(attributes, scene, mesh) { if (mesh.isVerticesDataPresent(attribute)) attributes.push(attribute); }
        getCustomCode(shaderType) {
            return shaderType === "vertex" ? {
                CUSTOM_VERTEX_DEFINITIONS: "#ifdef PORTAL_FOLIAGE_OCCLUSION\nattribute " + attribute + ": f32;\nvarying vPortalFoliageVisibility: f32;\n#endif\n",
                CUSTOM_VERTEX_MAIN_END: "#ifdef PORTAL_FOLIAGE_OCCLUSION\nvertexOutputs.vPortalFoliageVisibility = vertexInputs." + attribute + ";\n#endif\n"
            } : {
                CUSTOM_FRAGMENT_DEFINITIONS: "#ifdef PORTAL_FOLIAGE_OCCLUSION\nvarying vPortalFoliageVisibility: f32;\n#endif\n",
                CUSTOM_FRAGMENT_BEFORE_FRAGCOLOR: "#ifdef PORTAL_FOLIAGE_OCCLUSION\nfinalColor = vec4f(finalColor.rgb * fragmentInputs.vPortalFoliageVisibility, finalColor.a);\n#endif\n"
            };
        }
    }

    var previousStart = globalThis.__nativeArPortalStartProfile;
    globalThis.__nativeArPortalStartProfile = function (scene, status) {
        if (previousStart) previousStart(scene, status);
        var started = Date.now();
        var baked = 0, vertices = 0, darkest = 1;
        scene.meshes.forEach(function (mesh) {
            var material = mesh.material;
            var texture = material && (material.albedoTexture || material.diffuseTexture);
            if (!texture || !texture.hasAlpha || material.backFaceCulling || !(material.needAlphaBlending() || material.needAlphaTesting())) return;
            if (!mesh.getIndices || mesh.sourceMesh || mesh.isVerticesDataPresent(attribute)) return;
            var local = mesh.getVerticesData(BABYLON.VertexBuffer.PositionKind);
            var indices = mesh.getIndices();
            if (!local || !indices || indices.length < 192) return;
            var world = mesh.computeWorldMatrix(true);
            var positions = new Float32Array(local.length);
            var point = new BABYLON.Vector3();
            for (var i = 0; i < local.length; i += 3) {
                BABYLON.Vector3.TransformCoordinatesFromFloatsToRef(local[i], local[i + 1], local[i + 2], world, point);
                positions[i] = point.x; positions[i + 1] = point.y; positions[i + 2] = point.z;
            }
            var visibility = bake(positions, indices);
            if (!visibility) return;
            for (var k = 0; k < visibility.length; k++) {
                if (visibility[k] < darkest) darkest = visibility[k];
            }
            mesh.setVerticesData(attribute, visibility, false, 1);
            if (!material.pluginManager || !material.pluginManager.getPlugin(pluginName)) new FoliageOcclusionPlugin(material);
            baked++;
            vertices += visibility.length;
        });
        status("native-xr-portal:foliage-occlusion=baked:meshes=" + baked + ":vertices=" + vertices + ":darkest=" + darkest.toFixed(2) + ":ms=" + (Date.now() - started));
    };
})();
