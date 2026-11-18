import { createRequire } from "node:module";
import { join } from "node:path";

const source = process.env.BABYLON_BUILD_SOURCE;
if (!source) throw new Error("Run testBabylonSharedUniforms.mjs to select the maintained source tree.");
const ts = createRequire(join(source, "package.json"))("typescript");
export default {
    root: source,
    cacheDir: join(source, "../uniform-test-cache"),
    plugins: [{
        name: "typescript-standard-decorators",
        enforce: "pre",
        transform(code, id) {
            if (!id.endsWith(".ts") || id.includes("node_modules")) return;
            // Vite's native TS stripping leaves standard decorators for the JS runtime; Node cannot execute those yet.
            const result = ts.transpileModule(code, {
                fileName: id,
                compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.ESNext, useDefineForClassFields: false, sourceMap: true, inlineSources: true },
            });
            return { code: result.outputText, map: result.sourceMapText };
        },
    }],
    resolve: { alias: { core: join(source, "packages/dev/core/src") } },
    test: {
        include: [
            "packages/dev/core/test/unit/Materials/*uniformBuffer*.test.ts",
            "packages/dev/core/test/unit/Materials/materialPluginManager.declarations.test.ts",
            "packages/dev/core/test/unit/Engines/WebGPU/webgpuShaderProcessorWGSL*.test.ts",
            "packages/dev/core/test/unit/Engines/WebGPU/webgpuEngine.recordedDraws.test.ts",
            "packages/dev/core/test/unit/Meshes/babylon.mesh.recordedDraws.test.ts",
        ],
        pool: "forks",
        maxWorkers: 1,
    },
};
