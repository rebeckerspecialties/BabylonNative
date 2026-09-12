import { runShutdownTasks } from '@cts/common/framework/on_shutdown.js';
import { globalTestConfig } from '@cts/common/framework/test_config.js';
import { TestCaseRecorder } from '@cts/common/internal/logging/test_case_recorder.js';
import type { LiveTestCaseResult } from '@cts/common/internal/logging/result.js';
import { compareQueries, Ordering } from '@cts/common/internal/query/compare.js';
import { parseQuery } from '@cts/common/internal/query/parseQuery.js';
import { TestQueryMultiCase, TestQuerySingleCase } from '@cts/common/internal/query/query.js';
import { setGPUProvider } from '@cts/common/util/navigator_gpu.js';
import { g as compute } from '@cts/webgpu/api/operation/compute/basic.spec.js';
import { g as writeBuffer } from '@cts/webgpu/api/operation/queue/writeBuffer.spec.js';
import { g as clearBuffer } from '@cts/webgpu/api/operation/command_buffer/clearBuffer.spec.js';
import { g as copyBuffer } from '@cts/webgpu/api/operation/command_buffer/copyBufferToBuffer.spec.js';
import { g as copyTexture } from '@cts/webgpu/api/operation/command_buffer/copyTextureToTexture.spec.js';
import { g as errorScope } from '@cts/webgpu/api/validation/error_scope.spec.js';
import { g as uncaptured } from '@cts/webgpu/api/operation/uncapturederror.spec.js';
import { g as deviceLost } from '@cts/webgpu/api/operation/device/lost.spec.js';
import { g as mapping } from '@cts/webgpu/api/validation/buffer/mapping.spec.js';
import manifest from './smoke.json';

declare const __ctsLog: (message: string) => void;
declare const __ctsDone: (success: boolean) => void;
declare const __ctsQueries: string[] | undefined;

class DiagnosticRecorder extends TestCaseRecorder {
  override threw(error: unknown) {
    // Preserve errors even when subsequent device cleanup never reaches finish().
    __ctsLog(JSON.stringify({ type: 'exception', message: String(error),
      stack: error instanceof Error ? error.stack : undefined }));
    super.threw(error);
  }
}

const groups = [
  ['api,operation,compute,basic', compute],
  ['api,operation,queue,writeBuffer', writeBuffer],
  ['api,operation,command_buffer,clearBuffer', clearBuffer],
  ['api,operation,command_buffer,copyBufferToBuffer', copyBuffer],
  ['api,operation,command_buffer,copyTextureToTexture', copyTexture],
  ['api,validation,error_scope', errorScope],
  ['api,operation,uncapturederror', uncaptured],
  ['api,operation,device,lost', deviceLost],
  ['api,validation,buffer,mapping', mapping],
] as const;

async function main() {
  setGPUProvider(() => navigator.gpu);
  __ctsLog(JSON.stringify({ type: 'host', document: typeof document,
    HTMLCanvasElement: typeof HTMLCanvasElement, OffscreenCanvas: typeof OffscreenCanvas,
    GPUDevice: typeof GPUDevice }));
  // Serialize fixture ownership for this first, deterministic integration subset.
  globalTestConfig.maxSubcasesInFlight = 1;
  const queries = (typeof __ctsQueries === 'undefined' ? manifest.queries : __ctsQueries).map(parseQuery);
  const matched = queries.map(() => 0);
  const cases = [];
  const seen = new Set<string>();

  // Static spec registration replaces only the filesystem/dynamic-import loader.
  // Query selection and fixture execution remain CTS-owned, including subcase filtering.
  for (const [path, group] of groups) {
    const file = path.split(',');
    for (const test of group.iterate()) {
      for (const [index, query] of queries.entries()) {
        const testQuery = new TestQueryMultiCase('webgpu', file, test.testPath, {});
        if (compareQueries(testQuery, query) === Ordering.Unordered) continue;
        for (const testCase of test.iterate('params' in query ? query.params : null)) {
          const selfQuery = new TestQuerySingleCase('webgpu', file, testCase.id.test, testCase.id.params);
          if (query instanceof TestQuerySingleCase && compareQueries(selfQuery, query) !== Ordering.Equal) continue;
          const name = selfQuery.toString();
          if (seen.has(name)) throw new Error(`Overlapping CTS queries: ${name}`);
          seen.add(name);
          ++matched[index];
          cases.push({ name, selfQuery, testCase, subcases: testCase.computeSubcaseCount() });
        }
      }
    }
  }
  for (const [index, count] of matched.entries()) {
    if (!count) throw new Error(`CTS query matched no cases: ${queries[index]}`);
  }
  if (!cases.length) throw new Error('CTS selection is empty');
  __ctsLog(JSON.stringify({ type: 'inventory', ctsRevision: manifest.ctsRevision,
    queries: queries.map(String), cases: cases.map(({ name, subcases }) => ({ name, subcases })) }));
  const counts: Record<string, number> = {};
  try {
    for (const { name, selfQuery, testCase, subcases } of cases) {
      __ctsLog(JSON.stringify({ type: 'start', name, subcases }));
      const result: LiveTestCaseResult = { status: 'running', timems: -1 };
      const recorder = new DiagnosticRecorder(result, false);
      let timer: ReturnType<typeof setTimeout> | undefined;
      try {
        await Promise.race([
          testCase.run(recorder, selfQuery, []),
          new Promise<never>((_, reject) => {
            timer = setTimeout(() => reject(new Error('CTS case did not finalize within 30 seconds')), 30000);
          }),
        ]);
      } catch (error) {
        __ctsLog(JSON.stringify({ type: 'incomplete', name, subcases, result }));
        const stats = (navigator.gpu as GPU & { _backendStats?: () => unknown })._backendStats?.();
        if (stats) __ctsLog(JSON.stringify({ type: 'native-stats', stats }));
        throw error; // Never reuse a fixture/device whose cleanup is still pending.
      } finally {
        clearTimeout(timer);
      }
      counts[result.status] = (counts[result.status] || 0) + 1;
      __ctsLog(JSON.stringify({ type: 'case', name, subcases, result }));
    }
  } finally {
    runShutdownTasks();
  }
  __ctsLog(JSON.stringify({ type: 'summary', ctsRevision: manifest.ctsRevision, counts,
    cases: cases.length, subcases: cases.reduce((sum, c) => sum + c.subcases, 0) }));
  __ctsDone(counts.pass === cases.length);
}

main().catch(error => {
  __ctsLog(JSON.stringify({ type: 'fatal', message: String(error), stack: error?.stack }));
  __ctsDone(false);
});
