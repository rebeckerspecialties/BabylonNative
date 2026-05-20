#!/usr/bin/env node

const childProcess = require("child_process");
const fs = require("fs");
const os = require("os");
const path = require("path");

const BABYLON = require("../node_modules/babylonjs");
require("../node_modules/babylonjs-loaders");

// Converts the original Hill Valley .babylon scene plus loose image assets into
// the local GLB/KTX package used by the NativeXR portal demo. Geometry is
// quantized in GLB and textures are transcoded offline to GPU-native ASTC KTX,
// avoiding runtime CPU decompression on iOS devices.

const COMPONENT_SHORT = 5122;
const COMPONENT_UNSIGNED_SHORT = 5123;
const COMPONENT_UNSIGNED_INT = 5125;
const COMPONENT_FLOAT = 5126;
const ARRAY_BUFFER = 34962;
const ELEMENT_ARRAY_BUFFER = 34963;

function fail(message) {
    console.error(message);
    process.exit(1);
}

function parseArgs(argv) {
    const args = {
        source: path.resolve(__dirname, "../Playground/Scripts/native_xr_portal_assets/native_xr_portal_hillvalley.babylon"),
        output: path.resolve(__dirname, "../Playground/Scripts/native_xr_portal_assets/native_xr_portal_hillvalley.astc.glb"),
        astcBlock: "8x8",
        basisu: process.env.BASISU || "basisu",
        effort: "6",
        skipTextures: false,
    };

    for (let index = 2; index < argv.length; index++) {
        const arg = argv[index];
        const next = argv[index + 1];
        if (arg === "--source" && next) {
            args.source = path.resolve(next);
            index++;
        } else if (arg === "--output" && next) {
            args.output = path.resolve(next);
            index++;
        } else if (arg === "--astc-block" && next) {
            args.astcBlock = next;
            index++;
        } else if (arg === "--basisu" && next) {
            args.basisu = next;
            index++;
        } else if (arg === "--effort" && next) {
            args.effort = next;
            index++;
        } else if (arg === "--skip-textures") {
            args.skipTextures = true;
        } else {
            fail("Usage: transcodeHillValleyToGltf.js [--source file.babylon] [--output file.glb] [--astc-block 8x8] [--basisu path] [--effort 6] [--skip-textures]");
        }
    }

    if (!/^(4x4|5x4|5x5|6x5|6x6|8x5|8x6|8x8|10x5|10x6|10x8|10x10|12x10|12x12)$/.test(args.astcBlock)) {
        fail(`Unsupported ASTC block '${args.astcBlock}'.`);
    }

    return args;
}

function alignTo(value, alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

function basenameWithoutExtension(file) {
    return path.basename(file).replace(/\.[^.]+$/, "");
}

function basenameFromTexture(texture) {
    const name = texture && (texture.name || texture.url || texture._url);
    if (!name) {
        return null;
    }

    return path.basename(String(name).replace(/^file:\/\//, ""));
}

function colorFactorFromMaterial(material, alpha) {
    const diffuse = material.diffuseColor && material.diffuseColor.asArray ? material.diffuseColor.asArray() : [0, 0, 0];
    const emissive = material.emissiveColor && material.emissiveColor.asArray ? material.emissiveColor.asArray() : [0, 0, 0];
    const source = diffuse.some((value) => value > 0.001) ? diffuse : emissive;
    return [
        Math.max(0, Math.min(1, Number(source[0] ?? 1))),
        Math.max(0, Math.min(1, Number(source[1] ?? 1))),
        Math.max(0, Math.min(1, Number(source[2] ?? 1))),
        alpha,
    ];
}

function clamp01(value) {
    return Math.max(0, Math.min(1, Number(value || 0)));
}

function cloneTextureInfo(info) {
    if (!info) {
        return null;
    }

    return JSON.parse(JSON.stringify(info));
}

function pushExtensionUsed(json, name) {
    if (!json.extensionsUsed) {
        json.extensionsUsed = [];
    }
    if (!json.extensionsUsed.includes(name)) {
        json.extensionsUsed.push(name);
    }
}

function isBackdropMaterialName(name) {
    return /pano|fond|mountain|sky|ciel|background|welcome/.test(String(name || "").toLowerCase());
}

function writeGlb(json, binaryChunk, outputPath) {
    const jsonBuffer = Buffer.from(JSON.stringify(json), "utf8");
    const jsonLength = alignTo(jsonBuffer.length, 4);
    const binLength = alignTo(binaryChunk.length, 4);
    const totalLength = 12 + 8 + jsonLength + 8 + binLength;
    const glb = Buffer.alloc(totalLength);

    let offset = 0;
    glb.writeUInt32LE(0x46546c67, offset);
    offset += 4;
    glb.writeUInt32LE(2, offset);
    offset += 4;
    glb.writeUInt32LE(totalLength, offset);
    offset += 4;

    glb.writeUInt32LE(jsonLength, offset);
    offset += 4;
    glb.writeUInt32LE(0x4e4f534a, offset);
    offset += 4;
    jsonBuffer.copy(glb, offset);
    glb.fill(0x20, offset + jsonBuffer.length, offset + jsonLength);
    offset += jsonLength;

    glb.writeUInt32LE(binLength, offset);
    offset += 4;
    glb.writeUInt32LE(0x004e4942, offset);
    offset += 4;
    binaryChunk.copy(glb, offset);

    fs.mkdirSync(path.dirname(outputPath), { recursive: true });
    fs.writeFileSync(outputPath, glb);
}

function addBinaryData(state, typedArray, target, byteStride) {
    const byteOffset = alignTo(state.binaryLength, 4);
    const source = Buffer.from(typedArray.buffer, typedArray.byteOffset, typedArray.byteLength);
    const paddedLength = byteOffset - state.binaryLength;
    if (paddedLength > 0) {
        state.binaryChunks.push(Buffer.alloc(paddedLength));
    }
    state.binaryChunks.push(Buffer.from(source));
    state.binaryLength = byteOffset + source.length;

    const bufferViewIndex = state.json.bufferViews.length;
    state.json.bufferViews.push({
        buffer: 0,
        byteOffset,
        byteLength: source.length,
        target,
    });
    if (byteStride) {
        state.json.bufferViews[bufferViewIndex].byteStride = byteStride;
    }
    return bufferViewIndex;
}

function componentCount(type) {
    switch (type) {
        case "VEC3":
            return 3;
        case "VEC2":
            return 2;
        case "SCALAR":
            return 1;
        default:
            throw new Error(`Unsupported accessor type '${type}'.`);
    }
}

function addAccessor(state, typedArray, componentType, type, target, options = {}) {
    const bufferView = addBinaryData(state, typedArray, target, options.byteStride);
    const accessor = {
        bufferView,
        byteOffset: 0,
        componentType,
        count: options.count ?? typedArray.length / componentCount(type),
        type,
    };
    if (options.normalized) {
        accessor.normalized = true;
    }
    if (options.minMax) {
        accessor.min = options.minMax.min;
        accessor.max = options.minMax.max;
    }
    const index = state.json.accessors.length;
    state.json.accessors.push(accessor);
    return index;
}

function minMaxVec3(values) {
    const min = [Number.POSITIVE_INFINITY, Number.POSITIVE_INFINITY, Number.POSITIVE_INFINITY];
    const max = [Number.NEGATIVE_INFINITY, Number.NEGATIVE_INFINITY, Number.NEGATIVE_INFINITY];
    for (let index = 0; index < values.length; index += 3) {
        for (let axis = 0; axis < 3; axis++) {
            const value = values[index + axis];
            min[axis] = Math.min(min[axis], value);
            max[axis] = Math.max(max[axis], value);
        }
    }
    return { min, max };
}

function quantizePositions(values) {
    const bounds = minMaxVec3(values);
    const center = [
        (bounds.min[0] + bounds.max[0]) * 0.5,
        (bounds.min[1] + bounds.max[1]) * 0.5,
        (bounds.min[2] + bounds.max[2]) * 0.5,
    ];
    const extent = [
        (bounds.max[0] - bounds.min[0]) * 0.5,
        (bounds.max[1] - bounds.min[1]) * 0.5,
        (bounds.max[2] - bounds.min[2]) * 0.5,
    ];
    const scale = Math.max(extent[0], extent[1], extent[2], 1e-6);
    const vertexCount = values.length / 3;
    const output = new Int16Array(vertexCount * 4);
    const min = [Number.POSITIVE_INFINITY, Number.POSITIVE_INFINITY, Number.POSITIVE_INFINITY];
    const max = [Number.NEGATIVE_INFINITY, Number.NEGATIVE_INFINITY, Number.NEGATIVE_INFINITY];

    for (let index = 0; index < values.length; index += 3) {
        const outputBase = (index / 3) * 4;
        for (let axis = 0; axis < 3; axis++) {
            const normalized = Math.max(-1, Math.min(1, (values[index + axis] - center[axis]) / scale));
            const quantized = Math.round(normalized * 32767);
            output[outputBase + axis] = quantized;
            min[axis] = Math.min(min[axis], quantized);
            max[axis] = Math.max(max[axis], quantized);
        }
    }

    return {
        data: output,
        minMax: { min, max },
        matrix: [
            scale, 0, 0, 0,
            0, scale, 0, 0,
            0, 0, scale, 0,
            center[0], center[1], center[2], 1,
        ],
    };
}

function quantizeNormals(values) {
    const vertexCount = values.length / 3;
    const output = new Int16Array(vertexCount * 4);
    for (let index = 0; index < values.length; index += 3) {
        const outputBase = (index / 3) * 4;
        output[outputBase] = Math.round(Math.max(-1, Math.min(1, values[index])) * 32767);
        output[outputBase + 1] = Math.round(Math.max(-1, Math.min(1, values[index + 1])) * 32767);
        output[outputBase + 2] = Math.round(Math.max(-1, Math.min(1, values[index + 2])) * 32767);
    }
    return output;
}

function createExporterState(sourceDir, astcBlock, skipTextures) {
    const json = {
        asset: {
            version: "2.0",
            generator: `BabylonNative Hill Valley ASTC ${astcBlock} transcoder`,
        },
        extensionsUsed: ["KHR_mesh_quantization"],
        extensionsRequired: ["KHR_mesh_quantization"],
        buffers: [{ byteLength: 0 }],
        bufferViews: [],
        accessors: [],
        images: [],
        samplers: [{ wrapS: 10497, wrapT: 10497 }],
        textures: [],
        materials: [],
        meshes: [],
        nodes: [],
        scenes: [{ nodes: [] }],
        scene: 0,
    };

    return {
        json,
        sourceDir,
        astcBlock,
        skipTextures,
        binaryChunks: [],
        binaryLength: 0,
        imageByUri: new Map(),
        textureByUri: new Map(),
        textureSources: new Map(),
        materialByObject: new Map(),
        usedTextureTransform: false,
        vertexCount: 0,
        indexCount: 0,
    };
}

function addTexture(state, texture) {
    const sourceUri = basenameFromTexture(texture);
    if (!sourceUri) {
        return null;
    }
    const imagePath = path.join(state.sourceDir, sourceUri);
    if (!state.skipTextures && !fs.existsSync(imagePath)) {
        throw new Error(`Texture '${sourceUri}' referenced by '${texture.name}' does not exist next to the Babylon scene.`);
    }

    const uri = `${basenameWithoutExtension(sourceUri)}.ktx`;
    let imageIndex = state.imageByUri.get(uri);
    if (imageIndex === undefined) {
        imageIndex = state.json.images.length;
        state.json.images.push({ uri });
        state.imageByUri.set(uri, imageIndex);
        if (!state.skipTextures || fs.existsSync(imagePath)) {
            state.textureSources.set(uri, imagePath);
        }
    }

    let textureIndex = state.textureByUri.get(uri);
    if (textureIndex === undefined) {
        textureIndex = state.json.textures.length;
        state.json.textures.push({ sampler: 0, source: imageIndex });
        state.textureByUri.set(uri, textureIndex);
    }

    const info = { index: textureIndex };
    const texCoord = Number(texture.coordinatesIndex || 0);
    if (texCoord !== 0) {
        info.texCoord = texCoord;
    }

    const scaleX = Number(texture.uScale ?? 1);
    const scaleY = Number(texture.vScale ?? 1);
    const offsetX = Number(texture.uOffset || 0);
    const offsetY = Number(texture.vOffset || 0);
    const rotation = Number(texture.wAng || 0);
    if (scaleX !== 1 || scaleY !== 1 || offsetX !== 0 || offsetY !== 0 || rotation !== 0) {
        state.usedTextureTransform = true;
        info.extensions = {
            KHR_texture_transform: {
                offset: [offsetX, offsetY],
                scale: [scaleX, scaleY],
            },
        };
        if (rotation !== 0) {
            info.extensions.KHR_texture_transform.rotation = rotation;
        }
    }

    return info;
}

function addMaterial(state, material) {
    if (!material) {
        return undefined;
    }

    const cached = state.materialByObject.get(material);
    if (cached !== undefined) {
        return cached;
    }

    const alpha = Math.max(0, Math.min(1, Number(material.alpha ?? 1)));
    const baseTexture = material.diffuseTexture || material.opacityTexture || null;
    const name = material.name || `material_${state.json.materials.length}`;
    const materialJson = {
        name,
        pbrMetallicRoughness: {
            metallicFactor: 0,
            roughnessFactor: 1,
            baseColorFactor: baseTexture ? [1, 1, 1, alpha] : colorFactorFromMaterial(material, alpha),
        },
    };

    if (baseTexture) {
        materialJson.pbrMetallicRoughness.baseColorTexture = addTexture(state, baseTexture);
    }

    applyRealtimeMaterialHints(materialJson, material, name);
    if (isBackdropMaterialName(name)) {
        pushExtensionUsed(state.json, "KHR_materials_unlit");
    }

    if (material.backFaceCulling === false) {
        materialJson.doubleSided = true;
    }

    const usesAlphaTexture = Boolean(material.opacityTexture || (material.diffuseTexture && material.diffuseTexture.hasAlpha));
    if (alpha < 0.999 || usesAlphaTexture) {
        materialJson.alphaMode = "BLEND";
    }

    const index = state.json.materials.length;
    state.json.materials.push(materialJson);
    state.materialByObject.set(material, index);
    return index;
}

function applyRealtimeMaterialHints(materialJson, sourceMaterial, name) {
    const lowerName = String(name || "").toLowerCase();
    const pbr = materialJson.pbrMetallicRoughness;
    const specularPower = Number(sourceMaterial.specularPower || 0);
    const specularColor = sourceMaterial.specularColor && sourceMaterial.specularColor.asArray ? sourceMaterial.specularColor.asArray() : [0, 0, 0];
    const specularStrength = Math.max(specularColor[0] || 0, specularColor[1] || 0, specularColor[2] || 0);
    const emissiveColor = sourceMaterial.emissiveColor && sourceMaterial.emissiveColor.asArray ? sourceMaterial.emissiveColor.asArray() : [0, 0, 0];

    pbr.roughnessFactor = specularPower > 0 ? Math.max(0.16, Math.min(0.9, 1.0 - (Math.log2(specularPower + 1) / 9.0))) : 0.72;

    if (/metal|carrosserie|chrome|wheel|wheels|reactor|cable|cables|plate|vents|grill|grid|delorean|volant|convecteur/.test(lowerName)) {
        pbr.metallicFactor = /glass|vitre|seat|cable|grid|volant|convecteur/.test(lowerName) ? 0.35 : 0.82;
        pbr.roughnessFactor = Math.min(pbr.roughnessFactor, /carrosserie|chrome|plate/.test(lowerName) ? 0.22 : 0.36);
    }

    if (/glass|vitre|window/.test(lowerName)) {
        pbr.metallicFactor = 0;
        pbr.roughnessFactor = 0.08;
        materialJson.alphaMode = "BLEND";
        const base = pbr.baseColorFactor || [1, 1, 1, 1];
        base[3] = Math.min(base[3] ?? 1, /delorean/.test(lowerName) ? 0.42 : 0.62);
        pbr.baseColorFactor = base;
    }

    if (isBackdropMaterialName(lowerName)) {
        pbr.metallicFactor = 0;
        pbr.roughnessFactor = 1;
        materialJson.extensions = materialJson.extensions || {};
        materialJson.extensions.KHR_materials_unlit = {};
        materialJson.emissiveFactor = [1, 1, 1];
        if (pbr.baseColorTexture) {
            materialJson.emissiveTexture = cloneTextureInfo(pbr.baseColorTexture);
        }
        return;
    }

    if (/neon|light|lampe|lamp|feux|flare/.test(lowerName)) {
        const isLampFixture = /lampe|lamp/.test(lowerName) && !/neon|feux|flare|convecteur/.test(lowerName);
        const isSignalEmitter = /neon|feux|flare|convecteur|light/.test(lowerName);
        const intensity = isLampFixture ? 0.34 : (isSignalEmitter ? 0.82 : 0.52);
        const source = Math.max(emissiveColor[0] || 0, emissiveColor[1] || 0, emissiveColor[2] || 0) > 0.05 ? emissiveColor : [1, 1, 1];
        materialJson.emissiveFactor = [
            clamp01(source[0] * intensity),
            clamp01(source[1] * intensity),
            clamp01(source[2] * intensity),
        ];
        if (pbr.baseColorTexture) {
            materialJson.emissiveTexture = cloneTextureInfo(pbr.baseColorTexture);
        }
    }

    if (specularStrength > 0.35) {
        pbr.roughnessFactor = Math.min(pbr.roughnessFactor, 0.45);
    }
}

function addSourceLights(state, sourceJson) {
    const sourceLights = Array.isArray(sourceJson && sourceJson.lights) ? sourceJson.lights : [];
    const punctualLights = [];

    for (const light of sourceLights) {
        if (!light) {
            continue;
        }

        const lightType = Number(light.type);
        if (lightType !== 0 && lightType !== 1 && lightType !== 2) {
            continue;
        }

        const color = Array.isArray(light.diffuse) ? light.diffuse : [1, 1, 1];
        const gltfLight = {
            name: light.name || `light_${punctualLights.length}`,
            type: lightType === 0 ? "point" : (lightType === 1 ? "directional" : "spot"),
            color: [clamp01(color[0] ?? 1), clamp01(color[1] ?? 1), clamp01(color[2] ?? 1)],
            // Babylon StandardMaterial lights are non-physical. Give the glTF
            // punctual light enough energy to matter for PBR without baking.
            intensity: Math.max(1, Number(light.intensity || 1) * 850),
        };
        if (Number(light.range || 0) > 0) {
            gltfLight.range = Number(light.range);
        }
        if (gltfLight.type === "spot") {
            gltfLight.spot = {
                innerConeAngle: Number(light.innerAngle || 0) * 0.5,
                outerConeAngle: Number(light.angle || Math.PI / 3) * 0.5,
            };
        }

        const lightIndex = punctualLights.length;
        punctualLights.push(gltfLight);
        const position = Array.isArray(light.position) ? light.position : [0, 0, 0];
        const node = {
            name: light.name || `light_${lightIndex}`,
            translation: [-(Number(position[0] || 0)), Number(position[1] || 0), Number(position[2] || 0)],
            extensions: {
                KHR_lights_punctual: {
                    light: lightIndex,
                },
            },
        };
        state.json.scenes[0].nodes.push(state.json.nodes.length);
        state.json.nodes.push(node);
    }

    if (punctualLights.length) {
        pushExtensionUsed(state.json, "KHR_lights_punctual");
        state.json.extensions = state.json.extensions || {};
        state.json.extensions.KHR_lights_punctual = {
            lights: punctualLights,
        };
    }
}

function transformPositions(mesh, positions) {
    const world = mesh.computeWorldMatrix(true);
    const values = new Float32Array(positions.length);
    const input = new BABYLON.Vector3();
    const output = new BABYLON.Vector3();
    for (let index = 0; index < positions.length; index += 3) {
        input.set(positions[index], positions[index + 1], positions[index + 2]);
        BABYLON.Vector3.TransformCoordinatesToRef(input, world, output);
        // Babylon's glTF loader reflects X when importing into a left-handed scene.
        // Bake the inverse reflection so the loaded GLB matches the source .babylon frame.
        values[index] = -output.x;
        values[index + 1] = output.y;
        values[index + 2] = output.z;
    }
    return values;
}

function transformNormals(mesh, normals) {
    const world = mesh.computeWorldMatrix(true);
    const values = new Float32Array(normals.length);
    const input = new BABYLON.Vector3();
    const output = new BABYLON.Vector3();
    for (let index = 0; index < normals.length; index += 3) {
        input.set(normals[index], normals[index + 1], normals[index + 2]);
        BABYLON.Vector3.TransformNormalToRef(input, world, output);
        if (output.lengthSquared() < 1e-10) {
            output.set(0, 1, 0);
        } else {
            output.normalize();
        }
        values[index] = -output.x;
        values[index + 1] = output.y;
        values[index + 2] = output.z;
    }
    return values;
}

function addMesh(state, mesh) {
    const positions = mesh.getVerticesData(BABYLON.VertexBuffer.PositionKind);
    if (!positions || positions.length === 0) {
        return;
    }

    const normals = mesh.getVerticesData(BABYLON.VertexBuffer.NormalKind);
    const uvs = mesh.getVerticesData(BABYLON.VertexBuffer.UVKind);
    const sourceIndices = mesh.getIndices();
    if (!sourceIndices || sourceIndices.length === 0) {
        return;
    }
    const subMeshes = mesh.subMeshes && mesh.subMeshes.length ? mesh.subMeshes : [{ indexStart: 0, indexCount: sourceIndices.length, getMaterial: () => mesh.material }];

    const transformedPositions = transformPositions(mesh, positions);
    const quantizedPositions = quantizePositions(transformedPositions);
    const attributes = {
        POSITION: addAccessor(state, quantizedPositions.data, COMPONENT_SHORT, "VEC3", ARRAY_BUFFER, {
            normalized: true,
            minMax: quantizedPositions.minMax,
            count: positions.length / 3,
            byteStride: 8,
        }),
    };
    if (normals && normals.length === positions.length) {
        attributes.NORMAL = addAccessor(state, quantizeNormals(transformNormals(mesh, normals)), COMPONENT_SHORT, "VEC3", ARRAY_BUFFER, {
            normalized: true,
            count: normals.length / 3,
            byteStride: 8,
        });
    }
    if (uvs) {
        attributes.TEXCOORD_0 = addAccessor(state, new Float32Array(uvs), COMPONENT_FLOAT, "VEC2", ARRAY_BUFFER);
    }

    const primitives = [];
    for (const subMesh of subMeshes) {
        const indexStart = Number(subMesh.indexStart || 0);
        const indexCount = Number(subMesh.indexCount || 0);
        if (indexCount <= 0) {
            continue;
        }

        let maxIndex = 0;
        for (let index = 0; index < indexCount; index++) {
            maxIndex = Math.max(maxIndex, sourceIndices[indexStart + index]);
        }
        const useU16 = maxIndex <= 0xffff;
        const indices = useU16 ? new Uint16Array(indexCount) : new Uint32Array(indexCount);
        for (let index = 0; index < indexCount; index += 3) {
            indices[index] = sourceIndices[indexStart + index];
            indices[index + 1] = sourceIndices[indexStart + index + 1];
            indices[index + 2] = sourceIndices[indexStart + index + 2];
        }

        const material = typeof subMesh.getMaterial === "function" ? subMesh.getMaterial() : mesh.material;
        primitives.push({
            attributes,
            indices: addAccessor(state, indices, useU16 ? COMPONENT_UNSIGNED_SHORT : COMPONENT_UNSIGNED_INT, "SCALAR", ELEMENT_ARRAY_BUFFER),
            material: addMaterial(state, material),
            mode: 4,
        });
        state.indexCount += indexCount;
    }

    if (primitives.length === 0) {
        return;
    }

    const meshIndex = state.json.meshes.length;
    state.json.meshes.push({
        name: mesh.name,
        primitives,
    });
    const nodeIndex = state.json.nodes.length;
    state.json.nodes.push({
        name: mesh.name,
        mesh: meshIndex,
        matrix: quantizedPositions.matrix,
    });
    state.json.scenes[0].nodes.push(nodeIndex);
    state.vertexCount += positions.length / 3;
}

function run(command, args, options = {}) {
    childProcess.execFileSync(command, args, {
        stdio: options.stdio || "pipe",
        cwd: options.cwd,
        env: process.env,
    });
}

function convertTextureToAstcKtx(basisu, sourcePath, outputPath, astcBlock, effort) {
    const tempRoot = fs.mkdtempSync(path.join(os.tmpdir(), "bn-hv-astc-"));
    const blockToken = `ASTC_LDR_${astcBlock.toUpperCase().replace("X", "x").replace("x", "X")}_RGBA`;
    const sourceBase = basenameWithoutExtension(sourcePath);
    const ktx2Path = path.join(tempRoot, `${sourceBase}.ktx2`);
    try {
        run(basisu, [
            "-ktx2",
            "-ktx2_no_zstandard",
            `-astc_ldr_${astcBlock}`,
            "-effort",
            String(effort),
            "-mipmap",
            "-file",
            sourcePath,
            "-output_path",
            tempRoot,
        ]);

        if (!fs.existsSync(ktx2Path)) {
            throw new Error(`basisu did not create ${ktx2Path}`);
        }

        run(basisu, ["-unpack", ktx2Path, "-ktx_only"], { cwd: tempRoot });
        const candidates = fs.readdirSync(tempRoot).filter((file) => file.endsWith(".ktx") && file.includes(blockToken));
        if (candidates.length !== 1) {
            throw new Error(`Expected one ${blockToken} KTX candidate for ${sourcePath}, found ${candidates.length}.`);
        }

        fs.mkdirSync(path.dirname(outputPath), { recursive: true });
        fs.renameSync(path.join(tempRoot, candidates[0]), outputPath);
    } finally {
        fs.rmSync(tempRoot, { recursive: true, force: true });
    }
}

function transcodeTextures(state, basisu, outputDir, effort, skipTextures) {
    if (skipTextures) {
        return { count: state.textureSources.size, bytes: 0 };
    }

    let count = 0;
    let bytes = 0;
    for (const [uri, sourcePath] of state.textureSources) {
        const outputPath = path.join(outputDir, uri);
        convertTextureToAstcKtx(basisu, sourcePath, outputPath, state.astcBlock, effort);
        count++;
        bytes += fs.statSync(outputPath).size;
        if (count % 10 === 0 || count === state.textureSources.size) {
            console.log(`Transcoded ${count}/${state.textureSources.size} ASTC textures`);
        }
    }
    return { count, bytes };
}

async function main() {
    const args = parseArgs(process.argv);
    if (!fs.existsSync(args.source)) {
        fail(`Source scene '${args.source}' was not found. The packaged playground keeps only optimized ASTC assets; pass --source with a local Hill Valley .babylon source tree to regenerate them.`);
    }
    const sourceDir = path.dirname(args.source);
    const sourceText = fs.readFileSync(args.source, "utf8");
    const sourceJson = JSON.parse(sourceText);
    const engine = new BABYLON.NullEngine();
    const scene = new BABYLON.Scene(engine);
    const result = await BABYLON.SceneLoader.ImportMeshAsync("", "", `data:${sourceText}`, scene);
    const state = createExporterState(sourceDir, args.astcBlock, args.skipTextures);

    for (const mesh of result.meshes) {
        if (mesh && mesh.getTotalVertices && mesh.getTotalVertices() > 0) {
            addMesh(state, mesh);
        }
    }
    addSourceLights(state, sourceJson);

    if (state.usedTextureTransform) {
        pushExtensionUsed(state.json, "KHR_texture_transform");
    }

    const binaryChunk = Buffer.concat(state.binaryChunks);
    state.json.buffers[0].byteLength = binaryChunk.length;
    writeGlb(state.json, binaryChunk, args.output);
    const textureStats = transcodeTextures(state, args.basisu, path.dirname(args.output), args.effort, args.skipTextures);

    const sourceSize = fs.statSync(args.source).size;
    const outputSize = fs.statSync(args.output).size;
    console.log(`Wrote ${args.output}`);
    console.log(`Meshes: ${state.json.meshes.length}, primitives: ${state.json.meshes.reduce((count, mesh) => count + mesh.primitives.length, 0)}, vertices: ${state.vertexCount}, indices: ${state.indexCount}`);
    console.log(`Materials: ${state.json.materials.length}, images: ${state.json.images.length}`);
    console.log(`Source JSON: ${(sourceSize / 1024 / 1024).toFixed(2)} MiB, quantized GLB: ${(outputSize / 1024 / 1024).toFixed(2)} MiB, ASTC KTX: ${(textureStats.bytes / 1024 / 1024).toFixed(2)} MiB`);
}

main().catch((error) => {
    console.error(error && error.stack ? error.stack : error);
    process.exit(1);
});
