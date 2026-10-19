import fs from 'node:fs/promises';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const packageRoot = path.join(root, 'npm/velocitex');
const repository = 'https://github.com/TaiRuiQu/VelociTex.git';

export function releaseVersion(tag) {
  const match = /^v((?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?)$/.exec(tag);
  if (!match || match[0] !== tag) throw new Error('Expected vMAJOR.MINOR.PATCH[-prerelease].');
  const version = match[1];
  if (version.includes('-') && version.slice(version.indexOf('-') + 1).split('.').some(id => /^0\d+$/.test(id))) {
    throw new Error('Numeric prerelease identifiers cannot have leading zeroes.');
  }
  return { version, distTag: version.includes('-') ? 'next' : 'latest' };
}

export async function stagePackage(source, destination, version) {
  releaseVersion('v' + version);
  const metadata = JSON.parse(await fs.readFile(path.join(source, 'package.json'), 'utf8'));
  const lock = JSON.parse(await fs.readFile(path.join(source, 'package-lock.json'), 'utf8'));
  if (metadata.name !== 'velocitex' || lock.name !== metadata.name || lock.version !== metadata.version || lock.packages[''].version !== metadata.version) {
    throw new Error('Source npm package metadata and lock do not match.');
  }
  await fs.mkdir(destination, { recursive: true });
  for (const entry of ['dist', 'images', 'scripts', 'licenses', 'README.md', 'LICENSE', 'THIRD-PARTY-NOTICES.md']) {
    await fs.cp(path.join(source, entry), path.join(destination, entry), { recursive: true });
  }
  metadata.version = version;
  lock.version = version; lock.packages[''].version = version;
  await fs.writeFile(path.join(destination, 'package.json'), JSON.stringify(metadata, null, 2) + '\n');
  await fs.writeFile(path.join(destination, 'package-lock.json'), JSON.stringify(lock, null, 2) + '\n');
  const infoPath = path.join(destination, 'dist/build-info.json');
  const info = JSON.parse(await fs.readFile(infoPath, 'utf8'));
  info.packageVersion = version; info.versionSource = 'release-tag';
  await fs.writeFile(infoPath, JSON.stringify(info, null, 2) + '\n');
}

export async function createManifest(packageDirectory, outputDirectory, expectedCommit) {
  const metadata = JSON.parse(await fs.readFile(path.join(packageDirectory, 'package.json'), 'utf8'));
  const { distTag } = releaseVersion('v' + metadata.version);
  if (metadata.name !== 'velocitex' || metadata.repository?.url !== repository || metadata.publishConfig?.registry !== 'https://registry.npmjs.org/' || metadata.publishConfig?.access !== 'public') {
    throw new Error('Unexpected npm package identity, repository or publish destination.');
  }
  const filename = `velocitex-${metadata.version}.tgz`;
  const tarball = path.join(outputDirectory, filename);
  const readTarJson = file => JSON.parse(execFileSync('tar', ['-xOf', tarball, `package/${file}`], { encoding: 'utf8' }));
  const packed = readTarJson('package.json');
  const info = readTarJson('dist/build-info.json');
  if (packed.name !== metadata.name || packed.version !== metadata.version || packed.repository?.url !== repository ||
      packed.publishConfig?.registry !== 'https://registry.npmjs.org/' || packed.publishConfig?.access !== 'public' ||
      info.configuration !== 'Release' || info.aot !== true || info.sourceRevision !== expectedCommit || info.packageVersion !== metadata.version) {
    throw new Error('Tarball does not match the expected version, source and Release AOT build.');
  }
  const sha256 = createHash('sha256').update(await fs.readFile(tarball)).digest('hex');
  const report = { schemaVersion: 1, name: packed.name, version: packed.version, distTag, filename, sha256,
    sourceRevision: info.sourceRevision, sourceHasChanges: info.sourceHasChanges, aot: info.aot, configuration: info.configuration };
  await fs.writeFile(path.join(outputDirectory, 'npm-release.json'), JSON.stringify(report, null, 2) + '\n');
  return report;
}

async function main() {
  const [mode, ...args] = process.argv.slice(2);
  if (mode === 'tag') {
    const { version, distTag } = releaseVersion(process.env.RELEASE_TAG ?? args[0]);
    const head = execFileSync('git', ['rev-parse', 'HEAD'], { cwd: root, encoding: 'utf8' }).trim();
    const target = execFileSync('git', ['rev-parse', '--verify', `refs/tags/${process.env.RELEASE_TAG ?? args[0]}^{commit}`], { cwd: root, encoding: 'utf8' }).trim();
    if (head !== process.env.GITHUB_SHA || target !== head) throw new Error('Release tag does not match the triggering source revision.');
    if (process.env.GITHUB_OUTPUT) await fs.appendFile(process.env.GITHUB_OUTPUT, `version=${version}\ndist_tag=${distTag}\n`);
    console.log(`npm release: ${version} (${distTag})`);
  } else if (mode === 'stage') {
    await stagePackage(packageRoot, path.resolve(args[0]), args[1]);
  } else if (mode === 'manifest') {
    const head = execFileSync('git', ['rev-parse', 'HEAD'], { cwd: root, encoding: 'utf8' }).trim();
    const report = await createManifest(path.resolve(args[0]), path.resolve(args[1]), head);
    if (process.env.GITHUB_OUTPUT) {
      await fs.appendFile(process.env.GITHUB_OUTPUT, `version=${report.version}\ndist_tag=${report.distTag}\nfilename=${report.filename}\nsha256=${report.sha256}\n`);
    }
    console.log(JSON.stringify(report));
  } else throw new Error('Usage: release.mjs tag | stage <destination> <version> | manifest <package-dir> <tarball-dir>');
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) await main();
