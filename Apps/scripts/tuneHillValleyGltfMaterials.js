#!/usr/bin/env node

const fs = require("fs");
const path = require("path");

const GLB_MAGIC = 0x46546c67;
const JSON_CHUNK = 0x4e4f534a;
const BIN_CHUNK = 0x004e4942;

// Applies the material hints discovered while matching the original portal
// demo: panoramas/backdrops stay KHR_materials_unlit + emissive, while the
// Delorean and city materials keep realtime PBR-friendly metallic/roughness
// values for WebGPU rendering.

function fail(message) {
    console.error(message);
    process.exit(1);
}

function alignTo(value, alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

function clamp01(value) {
    return Math.max(0, Math.min(1, Number(value || 0)));
}

function cloneJson(value) {
    return value ? JSON.parse(JSON.stringify(value)) : null;
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

function parseArgs(argv) {
    const args = {
        input: path.resolve(__dirname, "../Playground/Scripts/native_xr_portal_assets/native_xr_portal_hillvalley.astc.glb"),
        output: null,
    };

    for (let index = 2; index < argv.length; index++) {
        const arg = argv[index];
        const next = argv[index + 1];
        if (arg === "--input" && next) {
            args.input = path.resolve(next);
            index++;
        } else if (arg === "--output" && next) {
            args.output = path.resolve(next);
            index++;
        } else {
            fail("Usage: tuneHillValleyGltfMaterials.js [--input scene.glb] [--output scene.glb]");
        }
    }

    if (!args.output) {
        args.output = args.input;
    }

    return args;
}

function readGlb(filePath) {
    const glb = fs.readFileSync(filePath);
    if (glb.readUInt32LE(0) !== GLB_MAGIC || glb.readUInt32LE(4) !== 2) {
        throw new Error(`${filePath} is not a GLB v2 file.`);
    }

    let offset = 12;
    const chunks = [];
    while (offset + 8 <= glb.length) {
        const byteLength = glb.readUInt32LE(offset);
        const type = glb.readUInt32LE(offset + 4);
        const data = glb.slice(offset + 8, offset + 8 + byteLength);
        chunks.push({ type, data });
        offset += 8 + byteLength;
    }

    const jsonChunk = chunks.find((chunk) => chunk.type === JSON_CHUNK);
    const binChunk = chunks.find((chunk) => chunk.type === BIN_CHUNK);
    if (!jsonChunk || !binChunk) {
        throw new Error(`${filePath} is missing JSON or BIN chunks.`);
    }

    return {
        json: JSON.parse(jsonChunk.data.toString("utf8").trim()),
        bin: binChunk.data,
    };
}

function writeGlb(filePath, json, bin) {
    const jsonBuffer = Buffer.from(JSON.stringify(json), "utf8");
    const jsonLength = alignTo(jsonBuffer.length, 4);
    const binLength = alignTo(bin.length, 4);
    const glb = Buffer.alloc(12 + 8 + jsonLength + 8 + binLength);

    let offset = 0;
    glb.writeUInt32LE(GLB_MAGIC, offset);
    offset += 4;
    glb.writeUInt32LE(2, offset);
    offset += 4;
    glb.writeUInt32LE(glb.length, offset);
    offset += 4;
    glb.writeUInt32LE(jsonLength, offset);
    offset += 4;
    glb.writeUInt32LE(JSON_CHUNK, offset);
    offset += 4;
    jsonBuffer.copy(glb, offset);
    glb.fill(0x20, offset + jsonBuffer.length, offset + jsonLength);
    offset += jsonLength;
    glb.writeUInt32LE(binLength, offset);
    offset += 4;
    glb.writeUInt32LE(BIN_CHUNK, offset);
    offset += 4;
    bin.copy(glb, offset);

    fs.writeFileSync(filePath, glb);
}

function hintMaterial(material) {
    const lowerName = String(material.name || "").toLowerCase();
    const pbr = material.pbrMetallicRoughness || {};
    material.pbrMetallicRoughness = pbr;

    if (material.extensions) {
        delete material.extensions.KHR_materials_unlit;
        if (Object.keys(material.extensions).length === 0) {
            delete material.extensions;
        }
    }

    pbr.metallicFactor = Number(pbr.metallicFactor || 0);
    pbr.roughnessFactor = Number(pbr.roughnessFactor || 0.72);

    if (/metal|carrosserie|chrome|wheel|wheels|reactor|cable|cables|plate|vents|grill|grid|delorean|volant|convecteur/.test(lowerName)) {
        pbr.metallicFactor = /glass|vitre|seat|cable|grid|volant|convecteur/.test(lowerName) ? 0.35 : 0.82;
        pbr.roughnessFactor = Math.min(pbr.roughnessFactor, /carrosserie|chrome|plate/.test(lowerName) ? 0.22 : 0.36);
    } else {
        pbr.roughnessFactor = Math.min(Math.max(pbr.roughnessFactor, 0.48), 0.82);
    }

    if (/glass|vitre|window/.test(lowerName)) {
        pbr.metallicFactor = 0;
        pbr.roughnessFactor = 0.08;
        material.alphaMode = "BLEND";
        const base = pbr.baseColorFactor || [1, 1, 1, 1];
        base[3] = Math.min(base[3] ?? 1, /delorean/.test(lowerName) ? 0.42 : 0.62);
        pbr.baseColorFactor = base;
    }

    if (isBackdropMaterialName(lowerName)) {
        pbr.metallicFactor = 0;
        pbr.roughnessFactor = 1;
        material.extensions = material.extensions || {};
        material.extensions.KHR_materials_unlit = {};
        material.emissiveFactor = [1, 1, 1];
        if (pbr.baseColorTexture) {
            material.emissiveTexture = cloneJson(pbr.baseColorTexture);
        }
        return;
    }

    if (/neon|light|lampe|lamp|feux|flare/.test(lowerName)) {
        const isLampFixture = /lampe|lamp/.test(lowerName) && !/neon|feux|flare|convecteur/.test(lowerName);
        const isSignalEmitter = /neon|feux|flare|convecteur|light/.test(lowerName);
        const intensity = isLampFixture ? 0.34 : (isSignalEmitter ? 0.82 : 0.52);
        const source = (material.emissiveFactor && Math.max(material.emissiveFactor[0] || 0, material.emissiveFactor[1] || 0, material.emissiveFactor[2] || 0) > 0.9) ? material.emissiveFactor : [1, 1, 1];
        material.emissiveFactor = [
            clamp01(source[0] * intensity),
            clamp01(source[1] * intensity),
            clamp01(source[2] * intensity),
        ];
        if (pbr.baseColorTexture) {
            material.emissiveTexture = cloneJson(pbr.baseColorTexture);
        }
    } else {
        delete material.emissiveFactor;
        delete material.emissiveTexture;
    }
}

function ensureHillValleyPunctualLights(json) {
    if (json.extensions && json.extensions.KHR_lights_punctual && Array.isArray(json.extensions.KHR_lights_punctual.lights) && json.extensions.KHR_lights_punctual.lights.length) {
        return;
    }

    pushExtensionUsed(json, "KHR_lights_punctual");
    json.extensions = json.extensions || {};
    json.extensions.KHR_lights_punctual = {
        lights: [{
            name: "Omni01",
            type: "point",
            color: [1, 1, 1],
            intensity: 850,
        }],
    };
    json.nodes = json.nodes || [];
    const lightNodeIndex = json.nodes.length;
    json.nodes.push({
        name: "Omni01",
        translation: [-181.75267, 80.3010559, 7.8190484],
        extensions: {
            KHR_lights_punctual: {
                light: 0,
            },
        },
    });
    json.scenes = json.scenes || [{ nodes: [] }];
    json.scenes[0].nodes = json.scenes[0].nodes || [];
    json.scenes[0].nodes.push(lightNodeIndex);
}

function tune(json) {
    const extensionsUsed = new Set(json.extensionsUsed || []);
    extensionsUsed.delete("KHR_materials_unlit");
    json.extensionsUsed = Array.from(extensionsUsed);
    if (json.extensionsUsed.length === 0) {
        delete json.extensionsUsed;
    }

    for (const material of json.materials || []) {
        hintMaterial(material);
        if (isBackdropMaterialName(material.name)) {
            pushExtensionUsed(json, "KHR_materials_unlit");
        }
    }
    ensureHillValleyPunctualLights(json);
}

const args = parseArgs(process.argv);
const glb = readGlb(args.input);
tune(glb.json);
writeGlb(args.output, glb.json, glb.bin);
console.log(`Updated realtime PBR material hints in ${args.output}`);
