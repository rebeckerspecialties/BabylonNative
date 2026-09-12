import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

test('dependency patches apply once, repeat quietly, and reject incompatible sources', t => {
  const directory = mkdtempSync(join(tmpdir(), 'native-patch-test-'));
  t.after(() => rmSync(directory, { recursive: true, force: true }));
  const init = spawnSync('git', ['init', '--quiet', directory], { encoding: 'utf8' });
  assert.equal(init.status, 0, init.stderr);
  const source = join(directory, 'example.txt');
  const patch = join(directory, 'change.patch');
  writeFileSync(source, 'before\n');
  writeFileSync(patch, 'diff --git a/example.txt b/example.txt\n--- a/example.txt\n+++ b/example.txt\n@@ -1 +1 @@\n-before\n+after\n');
  const apply = () => spawnSync('cmake', [`-DPATCH_FILE=${patch}`, '-P',
    join(dirname(fileURLToPath(import.meta.url)), 'apply_patch_if_needed.cmake')],
  { cwd: directory, encoding: 'utf8' });
  for (let attempt = 0; attempt < 2; ++attempt) {
    const result = apply();
    assert.equal(result.status, 0, result.stderr);
    assert.equal(result.stderr, '');
    assert.equal(readFileSync(source, 'utf8'), 'after\n');
  }
  writeFileSync(source, 'unrelated\n');
  const incompatible = apply();
  assert.notEqual(incompatible.status, 0);
  assert.match(incompatible.stderr, /neither applies cleanly/);
  assert.equal(readFileSync(source, 'utf8'), 'unrelated\n');
});
