import { execFileSync, spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../../../..');
const { values, positionals } = parseArgs({ allowPositionals: true,
  options: { 'native-only': { type: 'boolean', default: false } } });
if (positionals.length > 3) throw new Error('Expected native executable, evidence directory, and optional query manifest');
const native = resolve(positionals[0] || resolve(root,
  'build_wgpu_pr_stack/canonical-build/Plugins/NativeWebGPU/Tests/NativeWebGPUCtsRunner'));
const output = resolve(positionals[1] || resolve(root, 'build_wgpu_cts/results',
  new Date().toISOString().replaceAll(':', '-')));
const bundle = resolve(root, 'build_wgpu_cts/cts-smoke.js');
const manifest = JSON.parse(await readFile(`${bundle}.manifest.json`, 'utf8'));
const selection = positionals[2] ? JSON.parse(await readFile(resolve(positionals[2]), 'utf8')) : manifest;
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const bundleHash = hash(await readFile(bundle));
if (bundleHash !== manifest.sha256) throw new Error('Bundle does not match recorded manifest');
const git = args => execFileSync('git', args, { cwd: root, encoding: 'utf8' }).trim();
await mkdir(output, { recursive: true });
let buildCache = null;
try {
  const filename = resolve(dirname(native), '../../../CMakeCache.txt');
  const contents = await readFile(filename, 'utf8');
  buildCache = { filename, sha256: hash(contents), settings: contents.split('\n').filter(line =>
    /^(?:ENABLE_(?:SANITIZERS|THREAD_SANITIZER)|BABYLON_NATIVE_RUST_SANITIZERS|CMAKE_BUILD_TYPE|CARGO_EXECUTABLE|FETCHCONTENT_SOURCE_DIR_WGPU_NATIVE|NAPI_JAVASCRIPT_ENGINE):/.test(line)) };
} catch (error) {
  if (error.code !== 'ENOENT') throw error;
}
const evidence = {
  date: new Date().toISOString(), bundle: manifest, queries: selection.queries, native, nativeSha256: hash(await readFile(native)),
  nativeOnly: values['native-only'],
  buildCache,
  sanitizerEnvironment: Object.fromEntries(['ASAN_OPTIONS', 'UBSAN_OPTIONS', 'TSAN_OPTIONS'].map(key => [key, process.env[key] || null])),
  babylonNativeHead: git(['rev-parse', 'HEAD']), dirtyStatus: git(['status', '--porcelain']),
  localDiffSha256: hash(git(['diff', 'HEAD', '--binary'])), node: process.version,
  toolsLock: JSON.parse(await readFile(resolve(root, 'build_wgpu_cts/tools/package-lock.json'), 'utf8')),
  runs: [],
};

async function run(name, command, args, timeout = 60000) {
  console.log(`Running ${name}`);
  const started = Date.now();
  const result = await new Promise((resolveRun, reject) => {
    const child = spawn(command, args, { cwd: root, stdio: ['ignore', 'pipe', 'pipe'] });
    let stdout = '';
    let stderr = '';
    let timedOut = false;
    const timer = setTimeout(() => { timedOut = true; child.kill('SIGKILL'); }, timeout);
    child.stdout.setEncoding('utf8').on('data', data => { stdout += data; });
    child.stderr.setEncoding('utf8').on('data', data => { stderr += data; });
    child.on('error', error => { clearTimeout(timer); reject(error); });
    child.on('close', (code, signal) => {
      clearTimeout(timer);
      resolveRun({ code, signal, timedOut, stdout, stderr });
    });
  });
  await writeFile(resolve(output, `${name}.jsonl`), result.stdout);
  await writeFile(resolve(output, `${name}.stderr`), result.stderr);
  evidence.runs.push({ name, command, args, code: result.code, signal: result.signal,
    timedOut: result.timedOut, elapsedMs: Date.now() - started,
    stdoutSha256: hash(result.stdout), stderrSha256: hash(result.stderr) });
  await writeFile(resolve(output, 'evidence.json'), JSON.stringify(evidence, null, 2) + '\n');
  console.log(`${name}: exit=${result.code}, signal=${result.signal}, timeout=${result.timedOut}`);
}

if (!values['native-only']) {
  await run('dawn-control', process.execPath, [resolve(here, 'dawn-control.mjs'), bundle, ...selection.queries], 180000);
  await run('dawn-contracts', process.execPath, [resolve(here, 'dawn-control.mjs'), resolve(here, 'contracts.js')]);
}
await run('native-contracts', native, [resolve(here, 'contracts.js'), '15']);
// Each family gets a fresh process: a cleanup timeout cannot poison the next device/fixture.
for (const [index, query] of selection.queries.entries()) {
  await run(`native-family-${index}`, native, [bundle, '45', query]);
}
console.log(`Evidence: ${output}`);
process.exitCode = evidence.runs.every(run => run.code === 0 && !run.timedOut) ? 0 : 1;
