// Rewrites a .glb without its textures (images, textures, samplers, and every material texture slot), dropping
// EXT_texture_webp from extensionsUsed/Required. Mesh, skin, skeleton and animations are untouched (same BIN chunk).
import { readFileSync, writeFileSync } from 'node:fs';
const [src, dst] = process.argv.slice(2);
const b = readFileSync(src);
if (b.readUInt32LE(0) !== 0x46546c67) throw new Error('not a glb');
const jsonLen = b.readUInt32LE(12);
const json = JSON.parse(b.subarray(20, 20 + jsonLen).toString('utf8'));
const rest = b.subarray(20 + jsonLen);           // the BIN chunk header + data, kept as is
const before = { images: json.images?.length ?? 0, textures: json.textures?.length ?? 0, anims: json.animations?.length ?? 0, req: json.extensionsRequired };
delete json.images; delete json.textures; delete json.samplers;
for (const m of json.materials ?? []) {
  const p = m.pbrMetallicRoughness ?? {};
  delete p.baseColorTexture; delete p.metallicRoughnessTexture;
  delete m.normalTexture; delete m.occlusionTexture; delete m.emissiveTexture;
}
for (const k of ['extensionsUsed', 'extensionsRequired']) {
  if (json[k]) { json[k] = json[k].filter((e) => e !== 'EXT_texture_webp'); if (!json[k].length) delete json[k]; }
}
let out = Buffer.from(JSON.stringify(json), 'utf8');
const pad = (4 - (out.length % 4)) % 4;
out = Buffer.concat([out, Buffer.alloc(pad, 0x20)]);
const head = Buffer.alloc(20);
head.writeUInt32LE(0x46546c67, 0); head.writeUInt32LE(2, 4);
head.writeUInt32LE(20 + out.length + rest.length, 8);
head.writeUInt32LE(out.length, 12); head.writeUInt32LE(0x4e4f534a, 16);
writeFileSync(dst, Buffer.concat([head, out, rest]));
console.log('before', JSON.stringify(before), 'after extensionsRequired', JSON.stringify(json.extensionsRequired ?? []), 'anims', json.animations?.length, 'names', (json.animations ?? []).map((a) => a.name).join(' '));
