#!/usr/bin/env python3
"""Generate a *read-only*, compiled C++ byte array and coordinate registry."""
import json
import base64
from pathlib import Path
root = Path(__file__).resolve().parents[1]
master = root/'manager'/'DefaultTemplate_2000x2000px.png'
mapping = root/'manager'/'template_auto_map.dev.json'
output = root/'manager'/'ACModernUITemplateEmbedded.inl'
source = json.loads(mapping.read_text())
assert (source['width'],source['height']) == (2000,2000)
assert source['pixelCrc32'] == '0x8436C7CC'
entries=source['textures']
assert len(entries)==293 and sum(e['method']=='exact' for e in entries)==234
b=master.read_bytes()
with output.open('w',encoding='ascii',newline='\n') as f:
 f.write('// GENERATED FILE. Master PNG and v0.2 DID map compiled read-only.\n')
 f.write('// Do not hand-edit this file; regenerate with tools/generate_assets.py.\n')
 encoded = base64.b64encode(b).decode('ascii')
 f.write('static const char kTemplateMasterPngBase64[] =\n')
 for i in range(0,len(encoded),120):
  f.write('    "'+encoded[i:i+120]+'"\n')
 f.write('    ;\n')
 f.write('struct TemplateImportMapping { const char* did; unsigned x,y,w,h,pixelFormat; bool exact; };\n')
 f.write('static const TemplateImportMapping kTemplateImportMappings[] = {\n')
 for e in entries:
  f.write('    {"%s", %d, %d, %d, %d, %s, %s},\n' % (e['did'],e['x'],e['y'],e['width'],e['height'],e['pixelFormat'], 'true' if e['method']=='exact' else 'false'))
 f.write('};\n')
print('Wrote', output, 'PNG bytes', len(b),'mapped',len(entries))
