#!/usr/bin/env node

import { cpSync, existsSync, mkdirSync, readFileSync, readdirSync, realpathSync, rmSync } from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { parse } from "parse5";

function parseArguments(argv) {
    const options = { scenes: null, copyAssets: false };
    for (let index = 0; index < argv.length; ++index) {
        const argument = argv[index];
        if (argument === "--babylon-lite-root") {
            options.babylonLiteRoot = argv[++index];
        } else if (argument === "--output") {
            options.output = argv[++index];
        } else if (argument === "--asset-output") {
            options.assetOutput = argv[++index];
        } else if (argument === "--scenes") {
            options.scenes = argv[++index].split(",").map((value) => value.trim()).filter(Boolean);
        } else if (argument === "--copy-assets") {
            options.copyAssets = true;
        } else {
            throw new Error(`Unknown argument: ${argument}`);
        }
    }
    if (!options.babylonLiteRoot || !options.output) {
        throw new Error("Usage: buildBabylonLiteNativeBundles.mjs --babylon-lite-root PATH --output PATH [--scenes 1,2] [--copy-assets --asset-output PATH]");
    }
    if (options.copyAssets && !options.assetOutput) {
        throw new Error("--copy-assets requires --asset-output.");
    }
    return options;
}

function normalizeSceneName(value) {
    const name = String(value).startsWith("scene") ? String(value) : `scene${value}`;
    if (!/^scene\d+$/.test(name)) {
        throw new Error(`Invalid scene name: ${value}`);
    }
    return name;
}

function copyPublicAssets(sourceRoot, destinationRoot) {
    mkdirSync(destinationRoot, { recursive: true });
    for (const entry of readdirSync(sourceRoot, { withFileTypes: true })) {
        if (entry.name === "bundle" || entry.name === "thumbnails") {
            continue;
        }
        cpSync(path.join(sourceRoot, entry.name), path.join(destinationRoot, entry.name), {
            recursive: true,
            force: true
        });
    }
}

function getAttribute(node, name) {
    return node.attrs?.find((attribute) => attribute.name === name)?.value;
}

function getTextContent(node) {
    if (node.nodeName === "#text") {
        return node.value || "";
    }
    return (node.childNodes || []).map(getTextContent).join("");
}

function findDescendants(node, tagName, matches = []) {
    for (const child of node.childNodes || []) {
        if (child.tagName === tagName) {
            matches.push(child);
        }
        findDescendants(child, tagName, matches);
    }
    return matches;
}

function readDocumentDescriptors(htmlPath) {
    const document = parse(readFileSync(htmlPath, "utf8"));
    const descriptors = [];

    function visit(node) {
        const id = getAttribute(node, "id");
        if (node.tagName && id) {
            const attributes = Object.fromEntries((node.attrs || []).map((attribute) => [attribute.name, attribute.value]));
            const descriptor = {
                id,
                tagName: node.tagName,
                attributes,
                textContent: getTextContent(node)
            };

            if (node.tagName === "textarea") {
                descriptor.value = descriptor.textContent;
            } else if (node.tagName === "input") {
                descriptor.value = getAttribute(node, "value") || "";
                descriptor.checked = getAttribute(node, "checked") !== undefined;
            } else if (node.tagName === "select") {
                const options = findDescendants(node, "option");
                const selected = options.find((option) => getAttribute(option, "selected") !== undefined) || options[0];
                descriptor.value = selected ? (getAttribute(selected, "value") ?? getTextContent(selected)) : "";
            }

            descriptors.push(descriptor);
        }
        for (const child of node.childNodes || []) {
            visit(child);
        }
    }

    visit(document);
    return descriptors;
}

const options = parseArguments(process.argv.slice(2));
const babylonLiteRoot = realpathSync(path.resolve(options.babylonLiteRoot));
const outputRoot = path.resolve(options.output);
const sceneSourceRoot = path.join(babylonLiteRoot, "lab/lite/src/lite");
const liteBuildRoot = path.join(babylonLiteRoot, "packages/babylon-lite/build/lib");
const esbuildPath = path.join(babylonLiteRoot, "node_modules/esbuild/lib/main.js");

if (!existsSync(path.join(liteBuildRoot, "index.js"))) {
    throw new Error(`Babylon-Lite build output is missing at ${liteBuildRoot}. Run pnpm build:lib first.`);
}
if (!existsSync(esbuildPath)) {
    throw new Error(`esbuild is missing at ${esbuildPath}. Run pnpm install in Babylon-Lite first.`);
}

const availableScenes = readdirSync(sceneSourceRoot)
    .filter((name) => /^scene\d+\.ts$/.test(name))
    .map((name) => name.slice(0, -3))
    .sort((left, right) => Number(left.slice(5)) - Number(right.slice(5)));
const selectedScenes = options.scenes ? options.scenes.map(normalizeSceneName) : availableScenes;
for (const scene of selectedScenes) {
    if (!availableScenes.includes(scene)) {
        throw new Error(`Scene source not found: ${scene}`);
    }
}

const { build } = await import(pathToFileURL(esbuildPath));
if (!options.scenes) {
    rmSync(outputRoot, { recursive: true, force: true });
}
mkdirSync(outputRoot, { recursive: true });
if (options.scenes) {
    for (const scene of selectedScenes) {
        rmSync(path.join(outputRoot, `${scene}.playground.js`), { force: true });
    }
}

const liteResolvePlugin = {
    name: "babylon-lite-native-resolve",
    setup(esbuild) {
        esbuild.onResolve({ filter: /^babylon-lite(?:\/.*)?$/ }, (args) => {
            let suffix = args.path.slice("babylon-lite".length);
            if (!suffix) {
                suffix = "/index.js";
            } else if (!suffix.endsWith(".js")) {
                suffix += ".js";
            }
            return { path: path.join(liteBuildRoot, suffix) };
        });

        // These imports register runtime resolvers and samplers. The generated
        // package currently marks the entire tree side-effect-free, so preserve
        // the two registration-only imports explicitly when esbuild bundles it.
        esbuild.onResolve({ filter: /(?:pbr-primitive-resolver|gltf-sampler-denorm)\.js$/ }, (args) => ({
            path: path.resolve(args.resolveDir, args.path),
            sideEffects: true
        }));
    }
};

let failed = 0;
for (const scene of selectedScenes) {
    const outputPath = path.join(outputRoot, `${scene}.playground.js`);
    try {
        const htmlCandidates = [
            path.join(babylonLiteRoot, `lab/lite/${scene}.html`),
            path.join(babylonLiteRoot, `lab/lite/bundle-${scene}.html`)
        ];
        const htmlPath = htmlCandidates.find(existsSync);
        const documentDescriptors = htmlPath ? readDocumentDescriptors(htmlPath) : [];
        await build({
            entryPoints: [path.join(sceneSourceRoot, `${scene}.ts`)],
            outfile: outputPath,
            bundle: true,
            splitting: false,
            format: "iife",
            platform: "browser",
            target: "es2022",
            sourcemap: false,
            minify: false,
            define: {
                "import.meta.url": JSON.stringify(`app:///Scripts/BabylonLite/${scene}.playground.js`)
            },
            banner: {
                js: `globalThis.__babylonLiteValidationScene = ${JSON.stringify(scene)}; globalThis.__babylonLitePopulateDocument(${JSON.stringify(documentDescriptors)}); requestAnimationFrame(function () {`
            },
            footer: {
                js: "});"
            },
            plugins: [liteResolvePlugin],
            logLevel: "warning"
        });
        console.log(`Built ${scene}: ${outputPath}`);
    } catch (error) {
        failed++;
        console.error(`Failed to build ${scene}:`, error);
    }
}

if (options.copyAssets) {
    copyPublicAssets(path.join(babylonLiteRoot, "lab/public"), path.resolve(options.assetOutput));
    console.log(`Copied Babylon-Lite public assets to ${path.resolve(options.assetOutput)}`);
}

if (failed > 0) {
    process.exitCode = 1;
}
