"""Validate bilingual formatting and the shipped archive without loading the DLL."""
from pathlib import Path
import argparse, configparser, hashlib, json, re, struct, subprocess, zipfile
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--require-clean', action='store_true', help='Require release provenance to match a clean Git commit.')
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
def language(name):
    p=configparser.ConfigParser(interpolation=None);p.optionxform=str
    p.read(root/'language'/name,encoding='utf-8-sig')
    return {(s,k):p[s][k] for s in p for k in p[s]}
cn,en=language('CN.ini'),language('EN.ini')
assert cn.keys()==en.keys()
def placeholders(s):
    s=re.sub(r'%%','',s)
    return sorted(re.findall(r'%(?:[-+ #0]*\d*(?:\.\d+)?(?:ll|[hlzjtL])?)[diuoxXfFeEgGaAcsp]|\{[^{}]*\}',s))
for k in cn:
    if k[0]=='Text':
        a=json.loads(cn[k]);b=json.loads(en[k]);assert placeholders(a)==placeholders(b),(k,a,b)
        assert not re.search(r'[\u3400-\u9fff]',b),(k,b)
def key(s):
    h=14695981039346656037
    for b in s.encode():h=((h^b)*1099511628211)&0xffffffffffffffff
    return f'{h:016x}'
registered=[]
for line in (root/'src/internal/Language/InterfaceText.inc').read_text(encoding='utf-8').splitlines():
    if line.startswith('PHOENIX_TEXT('): registered.append(json.loads(line[len('PHOENIX_TEXT('):-1]))
assert len(registered)==len(set(registered))
assert {key(s) for s in registered}=={k for section,k in cn if section=='Text'}
with zipfile.ZipFile(root/'dist/phoenix.zip') as z:
    assert z.testzip() is None
    names=z.namelist()
    expected=['phoenix.dll','phoenix.exe','language/CN.ini','language/EN.ini',
              'README.md','LICENSE','VERSION','release.json','SHA256SUMS.txt']
    expected += [p.relative_to(root).as_posix() for folder in ['lua','work_shop'] for p in (root/folder).rglob('*.lua')]
    notices={'LuaJIT.txt':'src/external/LuaJIT-2.1/COPYRIGHT',
             'ImGui.txt':'src/external/Minimal-D3D12-Hook-ImGui/ImGui/LICENSE.txt',
             'D3D12-Hook.txt':'src/external/Minimal-D3D12-Hook-ImGui/LICENSE',
             'MinHook.txt':'src/external/Minimal-D3D12-Hook-ImGui/MinHook/LICENSE.txt',
             'ezVMT.txt':'src/external/ezVMT/LICENSE','sol2.txt':'src/external/sol2/License.txt'}
    expected += ['licenses/'+n for n in notices]
    normalized={n.replace('\\','/'):n for n in names}
    assert len(normalized)==len(names), 'Duplicate archive entry'
    assert {n for n in normalized if not n.endswith('/')}==set(expected), names
    for name in expected:
        if name in ['release.json','SHA256SUMS.txt']:continue
        source=root/('release/phoenix.dll' if name=='phoenix.dll' else
                     'ASA_Loader/x64/Release/phoenix.exe' if name=='phoenix.exe' else
                     notices[name.split('/',1)[1]] if name.startswith('licenses/') else name)
        assert z.read(normalized[name])==source.read_bytes(),name
    assert not any(n.startswith(('cfg/','logs/')) or 'uasset' in n.lower() for n in normalized)
    manifest=json.loads(z.read('release.json'))
    assert manifest['product']=='phoenix' and manifest['architecture']=='x64'
    assert manifest['license_mode']=='Development' and manifest['protection']=='none'
    assert manifest['version']==(root/'VERSION').read_text().strip()
    if args.require_clean:
        assert manifest['source_dirty'] is False, manifest
        assert manifest['source_commit']==subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
        assert not subprocess.check_output(['git','status','--porcelain'],cwd=root,text=True).strip()
    sums={}
    for line in z.read('SHA256SUMS.txt').decode('utf-8-sig').splitlines():
        digest,name=line.split('  ',1)
        assert name not in sums and re.fullmatch('[0-9a-f]{64}',digest),line
        sums[name]=digest
    assert set(sums)==set(expected)-{'SHA256SUMS.txt'}
    for name,digest in sums.items():assert hashlib.sha256(z.read(normalized[name])).hexdigest()==digest,name
    for name in ['phoenix.exe','phoenix.dll']:
        data=z.read(name)
        assert data[:2]==b'MZ',name
        pe=struct.unpack_from('<I',data,0x3c)[0]
        assert data[pe:pe+4]==b'PE\0\0',name
        machine,sections=struct.unpack_from('<HH',data,pe+4)
        optional_size,flags=struct.unpack_from('<HH',data,pe+20)
        assert machine==0x8664 and struct.unpack_from('<H',data,pe+24)[0]==0x20b,name
        assert bool(flags&0x2000)==name.endswith('.dll'),name
        table=pe+24+optional_size
        section_names=[data[table+40*i:table+40*i+8].rstrip(b'\0') for i in range(sections)]
        assert b'.text' in section_names and b'.rdata' in section_names,(name,section_names)
    dll_hash=hashlib.sha256(z.read(normalized['phoenix.dll'])).hexdigest()
    pending=root/'dist/phoenix/phoenix.dll.pending'
    deployed=pending if pending.exists() else root/'dist/phoenix/phoenix.dll'
    assert dll_hash==hashlib.sha256(deployed.read_bytes()).hexdigest()
archive=root/'dist/phoenix.zip'
archive_sum=(root/'dist/phoenix.zip.sha256').read_text().split()[0]
assert hashlib.sha256(archive.read_bytes()).hexdigest()==archive_sum
print(f'{len(cn)} matching language keys; {len(registered)} translated source texts; exact package contents, native x64 PE, CRC, provenance and SHA256 checks passed. Deployment={deployed.name}; DLL SHA256={dll_hash}')
