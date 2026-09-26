"""Validate bilingual formatting and the shipped archive without loading the DLL."""
from pathlib import Path
import configparser, hashlib, json, re, zipfile
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
    expected=['phoenix.dll','phoenix.exe','language/CN.ini','language/EN.ini','使用说明.md','功能补充与对照.md']
    expected+=['lua/'+s.name for s in (root/'lua').glob('Phoenix_*.lua')]
    normalized={n.replace('\\','/'):n for n in names}
    for name in expected:assert name in normalized,(name,names)
    for name in expected:
        source=root/('release/phoenix.dll' if name=='phoenix.dll' else
                     'ASA_Loader/x64/Release/phoenix.exe' if name=='phoenix.exe' else
                     'docs/phoenix-使用说明.md' if name=='使用说明.md' else
                     'docs/phoenix-20260917-功能补充.md' if name=='功能补充与对照.md' else name)
        assert z.read(normalized[name])==source.read_bytes(),name
    assert not any(n.startswith(('cfg/','logs/')) or 'uasset' in n.lower() for n in normalized)
    dll_hash=hashlib.sha256(z.read(normalized['phoenix.dll'])).hexdigest()
    pending=root/'dist/phoenix/phoenix.dll.pending'
    deployed=pending if pending.exists() else root/'dist/phoenix/phoenix.dll'
    assert dll_hash==hashlib.sha256(deployed.read_bytes()).hexdigest()
print(f'{len(cn)} matching language keys; {len(registered)} translated source texts; placeholders, Lua files, archive CRC and packaged DLL hash passed. Deployment={deployed.name}; SHA256={dll_hash}')
