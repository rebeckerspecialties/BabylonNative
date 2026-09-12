import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { parseArgs } from 'node:util';

const { values } = parseArgs({ options: {
    repo: { type: 'string' },
    ref: { type: 'string' },
    config: { type: 'string' },
    'config-revision': { type: 'string' },
} });
if (!values.repo || !values.ref || (values.config && !values['config-revision'])) {
    throw new Error('Use --repo <Dawn checkout> --ref <commit>; an external --config also requires --config-revision <commit>.');
}
const apps = path.resolve(import.meta.dirname, '..');
const playground = path.join(apps, 'Playground');
const git = (...args) => execFileSync('git', ['-C', values.repo, ...args], { maxBuffer: 32 * 1024 * 1024 });
const referenceRevision = git('rev-parse', values.ref).toString().trim();
const configRevision = values['config-revision'] || referenceRevision;
if (!/^[0-9a-f]{40}$/.test(configRevision)) throw new Error('The config revision must be a full commit hash.');
const configPath = 'Apps/Playground/Scripts/config.json';
const configBytes = values.config ? readFileSync(values.config) : git('show', `${referenceRevision}:${configPath}`);
const incoming = JSON.parse(configBytes);
const existing = JSON.parse(readFileSync(path.join(playground, 'Scripts/config.json')));
const titles = new Set(existing.tests.map(test => test.title));
const additions = incoming.tests.filter(test => !titles.has(test.title));
const references = new Map();
const prepared = additions.map(test => {
    if (!test.referenceImage) {
        if (!test.excludeFromAutomaticTesting) throw new Error(`Enabled test lacks a reference: ${test.title}`);
        return test;
    }
    if (path.basename(test.referenceImage) !== test.referenceImage) throw new Error(`Unexpected reference path: ${test.referenceImage}`);
    const filename = `dawn-${test.referenceImage}`;
    if (!references.has(filename)) {
        const source = `Apps/Playground/ReferenceImages/${test.referenceImage}`;
        const bytes = git('show', `${referenceRevision}:${source}`);
        references.set(filename, { bytes, source, sha256: createHash('sha256').update(bytes).digest('hex') });
    }
    // Dawn captures particle simulations without pre-rendering the scene.
    // Native readiness renders can advance GPU particles even with animations
    // disabled, changing the captured state before validation starts.
    const readinessPolicy = test.title.startsWith('GPU Particles -') ? { renderReadinessPump: false } : {};
    return { ...test, ...readinessPolicy, referenceImage: filename };
});
// Resolve every source before writing any fixture; never replace a reference with renderer output.
for (const [filename, reference] of references) {
    writeFileSync(path.join(playground, 'ReferenceImages', filename), reference.bytes);
}
writeFileSync(path.join(playground, 'Scripts/config.dawn-webgpu.json'), JSON.stringify({ ...incoming, tests: prepared }, null, 4) + '\n');
const provenance = {
    repository: 'https://github.com/CedricGuillemet/BabylonNative',
    configRevision, referenceRevision,
    configSha256: createHash('sha256').update(configBytes).digest('hex'),
    importedTests: prepared.length,
    enabledForWebGPU: prepared.filter(test => !test.excludeFromAutomaticTesting && !test.excludedGraphicsApis?.includes('WebGPU') && !test.onlyVisual).length,
    references: [...references].map(([filename, { source, sha256 }]) => ({ filename, source, sha256 })),
};
writeFileSync(path.join(playground, 'Scripts/config.dawn-webgpu.provenance.json'), JSON.stringify(provenance, null, 4) + '\n');
console.log(`Imported ${provenance.importedTests} tests (${provenance.enabledForWebGPU} enabled for WebGPU), ${references.size} original reference images.`);
