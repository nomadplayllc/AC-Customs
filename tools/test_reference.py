#!/usr/bin/env python3
"""Deterministic reference tests for bundled master + coordinate map.
   Does not compile or replace the C++ app.
"""
import json,re,struct,zlib,base64
from pathlib import Path
from PIL import Image
root=Path(__file__).resolve().parents[1]
img=Image.open(root/'manager'/'DefaultTemplate_2000x2000px.png').convert('RGBA')
assert img.size==(2000,2000)
raw=img.tobytes('raw','BGRA')
assert zlib.crc32(raw)==0x8436C7CC
rows=json.loads((root/'manager'/'template_auto_map.dev.json').read_text())['textures']
assert len(rows)==293
assert sum(x['method']=='exact' for x in rows)==234
embedded=(root/'manager'/'ACModernUITemplateEmbedded.inl').read_text()
assert embedded.count('{"0600')==293
assert 'static const char kTemplateMasterPngBase64[]' in embedded
asset = base64.b64decode(''.join(re.findall(r'\"([A-Za-z0-9+/=]+)\"',embedded.split('struct TemplateImportMapping')[0])))
assert asset==(root/'manager'/'DefaultTemplate_2000x2000px.png').read_bytes()
for r in rows:
 assert 0<=r['x']<=2000-r['width']
 assert 0<=r['y']<=2000-r['height']
 assert r['pixelFormat'] in ('0x00000014','0x00000015')
# Every defined region should be stable on the original master
rmap={e['did']:e for e in rows}
def changes(other, r):
 d=0
 for y in range(r['y'],r['y']+r['height']):
  for x in range(r['x'],r['x']+r['width']):
   p=(y*2000+x)*4
   ch=4 if r['pixelFormat']=='0x00000015' else 3
   if raw[p:p+ch]!=other[p:p+ch]:d+=1
 return d
assert all(changes(raw,r)==0 for r in rows)
other=bytearray(raw)
r=rmap['06000133']; p=(r['y']*2000+r['x'])*4
other[p] ^= 0x7f
assert changes(other,r)==1
# BGR alpha-only changes should not count.
other=bytearray(raw)
other[p+3] ^= 255
assert changes(other,r)==0
r=rmap['06004CEC'];p=(r['y']*2000+r['x'])*4
other=bytearray(raw);other[p+3]^=255
assert changes(other,r)==1
print('PASS: embedded asset signature, 293 mapped bounds, 234 exact, all baseline unchanged, BGR/RGBA change classification.')
