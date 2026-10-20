import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { test } from 'node:test';
import { releaseVersion, stagePackage, createManifest, tarCommand } from '../../npm/velocitex/scripts/release.mjs';

test('shared v* release tags select the correct npm channel', () => {
  assert.deepEqual(releaseVersion('v1.2.3'), { version: '1.2.3', distTag: 'latest' });
  assert.deepEqual(releaseVersion('v1.0.0-rc.2'), { version: '1.0.0-rc.2', distTag: 'next' });
  assert.deepEqual(releaseVersion('v0.1.0-preview.0'), { version: '0.1.0-preview.0', distTag: 'next' });
});

test('ambiguous tags, malformed versions and numeric prerelease zeroes fail', () => {
  for (const tag of [
    '1.2.3',
    'npm-v1.2.3',
    'v01.2.3',
    'v1.2',
    'v1.2.3-01',
    'v1.2.3-rc.01',
    'v1.2.3+build',
    'v1.2.3\n',
    'v1.2.3;echo bad',
  ]) {
    assert.throws(() => releaseVersion(tag), undefined, tag);
  }
});

test('tag staging preserves source metadata and manifest binds the actual archive', async () => {
  const root = fileURLToPath(new URL('../../', import.meta.url));
  const scratch = path.join(root, 'artifacts/wasm/release-tests');
  await fs.mkdir(scratch, { recursive: true });
  const directory = await fs.mkdtemp(path.join(scratch, 'run-'));
  const source = path.join(directory, 'source');
  const destination = path.join(directory, 'staged');
  for (const entry of ['dist', 'images', 'scripts', 'licenses'])
    await fs.mkdir(path.join(source, entry), { recursive: true });
  await fs.writeFile(path.join(source, 'images/comparison.png'), 'rendering-example');
  for (const entry of ['README.md', 'LICENSE', 'THIRD-PARTY-NOTICES.md'])
    await fs.writeFile(path.join(source, entry), 'fixture');
  const metadata = {
    name: 'velocitex',
    version: '0.1.0-preview.1',
    repository: { url: 'https://github.com/TaiRuiQu/VelociTex.git' },
    publishConfig: { access: 'public', registry: 'https://registry.npmjs.org/' },
  };
  const lock = { name: 'velocitex', version: metadata.version, packages: { '': { version: metadata.version } } };
  const before = JSON.stringify(metadata);
  const locked = JSON.stringify(lock);
  await fs.writeFile(path.join(source, 'package.json'), before);
  await fs.writeFile(path.join(source, 'package-lock.json'), locked);
  const revision = 'a'.repeat(40);
  await fs.writeFile(
    path.join(source, 'dist/build-info.json'),
    JSON.stringify({
      configuration: 'Release',
      aot: true,
      sourceRevision: revision,
      sourceHasChanges: false,
      packageVersion: metadata.version,
    })
  );
  await stagePackage(source, destination, '1.0.0-rc.2');
  assert.equal(await fs.readFile(path.join(destination, 'images/comparison.png'), 'utf8'), 'rendering-example');
  assert.equal(await fs.readFile(path.join(source, 'package.json'), 'utf8'), before);
  assert.equal(await fs.readFile(path.join(source, 'package-lock.json'), 'utf8'), locked);
  const staged = JSON.parse(await fs.readFile(path.join(destination, 'package-lock.json'), 'utf8'));
  assert.equal(staged.version, '1.0.0-rc.2');
  assert.equal(staged.packages[''].version, '1.0.0-rc.2');
  const info = JSON.parse(await fs.readFile(path.join(destination, 'dist/build-info.json'), 'utf8'));
  assert.equal(info.versionSource, 'release-tag');
  assert.equal(info.sourceRevision, revision);
  assert.equal(info.sourceHasChanges, false);
  // Build a real archive, then independently reject a changed version, source or destination.
  const tarRoot = path.join(directory, 'tar');
  await fs.mkdir(tarRoot);
  await fs.cp(destination, path.join(tarRoot, 'package'), { recursive: true });
  const filename = path.join(directory, 'velocitex-1.0.0-rc.2.tgz');
  function pack() {
    execFileSync(tarCommand, ['-czf', filename, '-C', tarRoot, 'package']);
  }
  pack();
  const manifest = await createManifest(destination, directory, revision);
  assert.equal(manifest.version, '1.0.0-rc.2');
  assert.equal(manifest.distTag, 'next');
  assert.match(manifest.sha256, /^[a-f0-9]{64}$/);
  await assert.rejects(createManifest(destination, directory, 'b'.repeat(40)), /Tarball does not match/);
  const packedMetadata = { ...metadata, version: '1.0.0-rc.3' };
  await fs.writeFile(path.join(tarRoot, 'package/package.json'), JSON.stringify(packedMetadata));
  pack();
  await assert.rejects(createManifest(destination, directory, revision), /Tarball does not match/);
  packedMetadata.version = manifest.version;
  packedMetadata.publishConfig = { registry: 'https://invalid.example/', access: 'public' };
  await fs.writeFile(path.join(tarRoot, 'package/package.json'), JSON.stringify(packedMetadata));
  pack();
  await assert.rejects(createManifest(destination, directory, revision), /Tarball does not match/);
  // Preserve a valid fixture for the actual workflow's PowerShell gate tests.
  await fs.copyFile(path.join(destination, 'package.json'), path.join(tarRoot, 'package/package.json'));
  pack();
  await createManifest(destination, directory, revision);
  await fs.writeFile(path.join(scratch, 'last-fixture.txt'), directory);
});
