import { readdirSync, writeFileSync } from "node:fs";
import { createRequire } from "node:module";
import { join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { createLoopInvariantTransformer } from "./loopInvariantTransformer.mjs";

const source = resolve(process.argv[2] || fileURLToPath(new URL("../../build_babylon_bundle/source", import.meta.url)));
const output = process.argv[3];
const ts = createRequire(join(source, "package.json"))("typescript");
const root = join(source, "packages/dev/core/src");
const files = readdirSync(root, { recursive: true }).filter(name => name.endsWith(".ts") && !name.endsWith(".d.ts")).map(name => join(root, name));
// Lexical binding analysis only: no type-based claim that a property is pure.
const program = ts.createProgram(files, { target: ts.ScriptTarget.ES2022, noLib: true, noResolve: true });
const transforms = createLoopInvariantTransformer(ts, program);
for (const file of files) {
    const result = ts.transform(program.getSourceFile(file), transforms.before);
    result.dispose();
}
const report = transforms.report;
for (const entry of [...report.hoisted, ...report.rejectedProperties]) entry.file = relative(source, entry.file);
function countByFile(entries) {
    const counts = new Map();
    for (const entry of entries) counts.set(entry.file, (counts.get(entry.file) || 0) + 1);
    return [...counts].sort((a, b) => b[1] - a[1]);
}
const summary = { source, files: files.length, hoisted: report.hoisted.length,
    rejectedPropertyAccesses: report.rejectedProperties.length, hoistedByFile: countByFile(report.hoisted).slice(0, 20),
    hotProperties: report.rejectedProperties.filter(entry => /uniformBuffer.ts$|webgpuCacheRenderPipelineTree.ts$|webgpuRenderPassCommandStream.ts$/.test(entry.file)) };
if (output) writeFileSync(output, JSON.stringify({ ...summary, decisions: report }, null, 2) + "\n");
console.log(JSON.stringify(summary, null, 2));
