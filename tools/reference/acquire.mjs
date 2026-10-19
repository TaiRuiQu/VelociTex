import fs from 'node:fs/promises';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

export const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
export const lock = JSON.parse(await fs.readFile(path.join(root, 'docs/reference/katex-0.19.0.lock.json'), 'utf8'));
export const upstream = path.join(root, 'artifacts/upstream');
export const packageRoot = path.join(upstream, 'npm/package');
export const sourceRoot = path.join(upstream, 'source');
export const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
export async function writeJson(file, data) {
  await fs.mkdir(path.dirname(file), {recursive:true});
  await fs.writeFile(file, JSON.stringify(data, null, 2) + '\n');
}
async function get(url) {
  const response = await fetch(url, {headers: {'User-Agent':'VelociTex-reference/0.0.0'}});
  if (!response.ok) throw new Error(`${response.status} ${url}`);
  return Buffer.from(await response.arrayBuffer());
}
async function files(directory) {
  const result=[];
  for (const entry of await fs.readdir(directory, {withFileTypes:true})) {
    const file=path.join(directory,entry.name);
    if(entry.isDirectory()) result.push(...await files(file)); else result.push(file);
  }
  return result.sort();
}
export async function acquire() {
  await fs.mkdir(upstream, {recursive:true});
  const metadata = JSON.parse(await get(lock.npm.metadataUrl));
  if(metadata.version!==lock.npm.version || metadata.dist.integrity!==lock.npm.integrity) throw new Error('npm metadata differs from original lock');
  const tag=JSON.parse(await get(lock.source.referenceApi));
  if(tag.object.sha!==lock.source.commit || tag.object.type!=='commit') throw new Error('Tag does not resolve to locked commit');
  const tarball=await get(lock.npm.tarballUrl);
  if(sha256(tarball)!==lock.npm.sha256 || 'sha512-'+createHash('sha512').update(tarball).digest('base64')!==lock.npm.integrity) throw new Error('npm tarball hash mismatch');
  const npmTar=path.join(upstream,'katex-0.19.0.tgz');
  await fs.writeFile(npmTar,tarball);
  await fs.mkdir(path.join(upstream,'npm'), {recursive:true});
  execFileSync('tar',['-xzf',npmTar,'-C',path.join(upstream,'npm')]);
  const sourceUrl=`https://codeload.github.com/KaTeX/KaTeX/tar.gz/${lock.source.commit}`;
  const archive=await get(sourceUrl);
  const sourceTar=path.join(upstream,`${lock.source.commit}.tar.gz`);
  await fs.writeFile(sourceTar,archive);
  await fs.mkdir(sourceRoot,{recursive:true});
  execFileSync('tar',['-xzf',sourceTar,'-C',sourceRoot,'--strip-components=1']);
  // GitHub's trees API provides the commit's individual Git blob IDs. Verify
  // source bytes using Git's blob hash, rather than trusting an archive name.
  const tree=JSON.parse(await get(`https://api.github.com/repos/KaTeX/KaTeX/git/trees/${lock.source.commit}?recursive=1`));
  if(tree.truncated) throw new Error('Incomplete Git tree response');
  const blobs=new Map(tree.tree.filter(x=>x.type==='blob').map(x=>[x.path,x.sha]));
  const sourceEntries=[];
  for(const file of await files(sourceRoot)) {
    const relative=path.relative(sourceRoot,file).replaceAll('\\','/');
    const bytes=await fs.readFile(file);
    const gitHash=createHash('sha1').update(`blob ${bytes.length}\0`).update(bytes).digest('hex');
    if(blobs.get(relative)!==gitHash) throw new Error(`Source blob mismatch ${relative}`);
    sourceEntries.push({path:relative,bytes:bytes.length,sha256:sha256(bytes),gitBlob:gitHash});
  }
  if(sourceEntries.length!==blobs.size) throw new Error(`Git tree has ${blobs.size} files but archive has ${sourceEntries.length}`);
  const packageEntries=[];
  for(const file of await files(packageRoot)) {
    const bytes=await fs.readFile(file);
    packageEntries.push({path:path.relative(packageRoot,file).replaceAll('\\','/'),bytes:bytes.length,sha256:sha256(bytes)});
  }
  for(const font of lock.ttfFonts) {
    const found=packageEntries.find(x=>x.path===`dist/fonts/${font.file}`);
    if(!found || found.bytes!==font.bytes || found.sha256!==font.sha256) throw new Error(`Font mismatch ${font.file}`);
  }
  await fs.copyFile(path.join(sourceRoot,'LICENSE'),path.join(root,'licenses/KaTeX-LICENSE.txt'));
  const manifest={schemaVersion:1,sourceCommit:lock.source.commit,npmVersion:lock.npm.version,npmIntegrity:lock.npm.integrity,npmSha256:sha256(tarball),sourceArchive:{url:sourceUrl,sha256:sha256(archive),gitBlobVerification:true},sourceFiles:sourceEntries,packageFiles:packageEntries};
  await writeJson(path.join(root,'docs/reference/katex-0.19.0.files.json'),manifest);
  console.log(JSON.stringify({npmFiles:packageEntries.length,sourceFiles:sourceEntries.length,packageRoot,sourceRoot,sourceArchiveSha256:sha256(archive)}));
}
if(process.argv[1]===fileURLToPath(import.meta.url)) await acquire();
