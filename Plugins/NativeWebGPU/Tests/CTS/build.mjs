import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../../../..');
const cts = resolve(process.argv[2] || resolve(root, 'build_wgpu_cts/cts'));
const output = resolve(process.argv[3] || resolve(root, 'build_wgpu_cts/cts-smoke.js'));
const esbuild = await import(pathToFileURL(resolve(root, 'build_wgpu_cts/tools/node_modules/esbuild/lib/main.js')));
const manifest = JSON.parse(await readFile(resolve(here, 'smoke.json'), 'utf8'));
const revision = execFileSync('git', ['rev-parse', 'HEAD'], { cwd: cts, encoding: 'utf8' }).trim();
if (revision !== manifest.ctsRevision) throw new Error(`CTS revision mismatch: ${revision}`);
if (execFileSync('git', ['status', '--porcelain'], { cwd: cts, encoding: 'utf8' }).trim()) {
  throw new Error('CTS checkout must be unmodified');
}
await mkdir(dirname(output), { recursive: true });
const result = await esbuild.build({
  entryPoints: [resolve(here, 'runner.ts')], outfile: output,
  bundle: true, format: 'iife', platform: 'neutral', target: 'es2022',
  // Match CTS's ES2020 TypeScript class-field emit without installing its lint toolchain.
  tsconfigRaw: { compilerOptions: { target: 'es2020', useDefineForClassFields: false } },
  alias: { '@cts': resolve(cts, 'src') }, external: ['perf_hooks'],
  sourcemap: true, metafile: true,
});
const sha256 = createHash('sha256').update(await readFile(output)).digest('hex');
await writeFile(`${output}.manifest.json`, JSON.stringify({
  ctsRevision: revision, esbuildVersion: esbuild.version, sha256,
  queries: manifest.queries, inputs: Object.keys(result.metafile.inputs),
}, null, 2) + '\n');
console.log(JSON.stringify({ output, sha256, ctsRevision: revision }));
