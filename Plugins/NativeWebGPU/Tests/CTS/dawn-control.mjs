import { readFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { runInThisContext } from 'node:vm';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../../..');
const { create, globals } = await import(pathToFileURL(resolve(root, 'build_wgpu_cts/tools/node_modules/webgpu/index.js')));
Object.assign(globalThis, globals);
Object.defineProperty(globalThis, 'navigator', { value: { gpu: create(['backend=metal']) }, configurable: true });
const adapter = await navigator.gpu.requestAdapter();
if (!adapter) throw new Error('Dawn Metal adapter unavailable');
console.log(JSON.stringify({ type: 'adapter', provider: 'webgpu@0.6.0', backend: 'metal',
  info: { vendor: adapter.info.vendor, architecture: adapter.info.architecture,
    device: adapter.info.device, description: adapter.info.description } }));
globalThis.__ctsLog = message => console.log(message);
globalThis.__ctsDone = success => process.exit(success ? 0 : 1);
if (process.argv.length > 3) globalThis.__ctsQueries = process.argv.slice(3);
const path = resolve(process.argv[2] || resolve(root, 'build_wgpu_cts/cts-smoke.js'));
setTimeout(() => {
  console.error('Dawn CTS control timed out');
  process.exit(2);
}, 180000);
runInThisContext(await readFile(path, 'utf8'), { filename: path });
