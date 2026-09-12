import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../../..');
if (process.platform !== 'darwin') throw new Error('This configuration currently targets macOS/Metal');
const mode = process.argv[2] || 'asan';
if (!['asan', 'tsan', 'none'].includes(mode)) throw new Error('Expected asan, tsan, or none');
const destination = resolve(process.argv[3] || resolve(root, `build_wgpu_cts/${mode}`));
const args = ['-S', root, '-B', destination, '-G', 'Ninja'];
// Reuse only source locations and project options, never cached compiler/linker flags.
// Omitting the optional seed uses the repository's public dependency declarations.
if (process.argv[4]) {
  const cache = readFileSync(resolve(process.argv[4], 'CMakeCache.txt'), 'utf8');
  for (const line of cache.split('\n')) {
    const entry = line.match(/^((?:BABYLON_NATIVE_|NAPI_|FETCHCONTENT_SOURCE_DIR_)[^:]+):(BOOL|STRING|PATH|UNINITIALIZED)=(.*)$/);
    if (entry) args.push(`-D${entry[1]}:${entry[2]}=${entry[3]}`);
  }
}
args.push(
  '-DCMAKE_BUILD_TYPE=Debug', '-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0',
  `-DCMAKE_OSX_SYSROOT=${execFileSync('xcrun', ['--sdk', 'macosx', '--show-sdk-path'], { encoding: 'utf8' }).trim()}`,
  '-DBABYLON_NATIVE_BUILD_APPS=OFF', '-DBABYLON_NATIVE_BUILD_WEBGPU_TESTS=ON',
  '-DBABYLON_NATIVE_PLUGIN_NATIVEWEBGPU=ON', '-DBABYLON_NATIVE_PLUGIN_NATIVEXR=OFF',
  '-DBABYLON_NATIVE_EMBEDDING=OFF', '-DBABYLON_NATIVE_LTO_BITCODE_STATIC_LIBS=OFF',
  '-DBABYLON_NATIVE_WGPU_USE_UPSTREAM_NATIVE=ON', '-DBABYLON_NATIVE_WGPU_NATIVE_RLIB=ON',
  '-DNAPI_JAVASCRIPT_ENGINE=JavaScriptCore',
  `-DBABYLON_NATIVE_RUST_SANITIZERS=${mode === 'none' ? 'OFF' : 'ON'}`,
  `-DENABLE_SANITIZERS=${mode === 'asan' ? 'ON' : 'OFF'}`,
  `-DENABLE_THREAD_SANITIZER=${mode === 'tsan' ? 'ON' : 'OFF'}`,
);
if (mode !== 'none') {
  // Apple's SDK uses an enum extension and newer attribute spelling not enabled
  // by upstream LLVM. These are frontend compatibility flags, not sanitizer exclusions.
  for (const language of ['C', 'CXX', 'OBJC', 'OBJCXX']) {
    args.push(`-DCMAKE_${language}_FLAGS=-Wno-elaborated-enum-base -Wno-error=unknown-attributes`);
  }
}
execFileSync('cmake', args, { cwd: root, stdio: 'inherit' });
const configured = readFileSync(resolve(destination, 'CMakeCache.txt'), 'utf8');
for (const [name, enabled] of [
  ['ENABLE_SANITIZERS', mode === 'asan'],
  ['ENABLE_THREAD_SANITIZER', mode === 'tsan'],
  ['BABYLON_NATIVE_RUST_SANITIZERS', mode !== 'none'],
]) {
  if (!configured.split('\n').includes(`${name}:BOOL=${enabled ? 'ON' : 'OFF'}`))
    throw new Error(`Configuration changed requested sanitizer option ${name}`);
}
console.log(`Build: cmake --build ${destination} --target NativeWebGPUCtsRunner NativeWebGPUAsyncTests --parallel 6`);
