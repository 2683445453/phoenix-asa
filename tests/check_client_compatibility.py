"""Check SDK against the recorded native-client contract; optionally check an installed EXE.

This never launches the game, loads the DLL or writes process memory. A passing
source check is not evidence that a remote server accepts a gameplay feature.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


ROOT = Path(__file__).resolve().parents[1]


def check(executable=None):
    baseline = json.loads((ROOT / 'tests/fixtures/client-94.34.json').read_text(encoding='utf-8'))
    basic = (ROOT / 'src/external/CppSDK/SDK/Basic.hpp').read_text(encoding='utf-8-sig')
    assertions = (ROOT / 'src/external/CppSDK/Assertions.inl').read_text(encoding='utf-8-sig')
    hooks = (ROOT / 'src/internal/CheatData/CheatData.hpp').read_text(encoding='utf-8-sig')
    for name, value in baseline['offsets'].items():
        found = re.search(r'constexpr int32 ' + re.escape(name) + r'\s*=\s*(0x[\da-fA-F]+)', basic)
        assert found and int(found[1], 16) == int(value, 16), f'SDK offset drift: {name}'
    values = {expr: int(value, 16) for expr, value in
              re.findall(r'static_assert\((.+?) == (0x[\da-fA-F]+)', assertions)}
    checked = 0
    for name, contract in baseline['native_classes'].items():
        assert values.get(f'sizeof({name})') == contract['size'], f'Native size drift: {name}'
        for field, offset in contract['fields'].items():
            assert values.get(f'offsetof({name}, {field})') == offset, f'Native field drift: {name}.{field}'
            checked += 1
    assert checked == baseline['fields_checked']
    for name, contract in baseline['hooks'].items():
        found = re.search(r'inline std::string ' + re.escape(name) + r' = "([0-9A-Fa-f ?]+)";', hooks)
        assert found and found[1] == contract['pattern'], f'Hook signature drift: {name}'
    patch = (ROOT / 'src/internal/Hack/FeatureExpansion.inl').read_text(encoding='utf-8-sig')
    site = re.search(r'SiteRva\s*=\s*(0x[\da-fA-F]+)', patch)
    assert site and int(site[1], 16) == int(baseline['mounted_yaw_site'], 16), 'Mounted camera site drift'

    if executable:
        data = Path(executable).read_bytes()
        assert hashlib.sha256(data).hexdigest().upper() == baseline['sha256'], \
            'Unknown client executable: revalidate the SDK before using this build.'
        assert data[:2] == b'MZ', 'Invalid PE image'
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        assert data[pe:pe+4] == b'PE\0\0', 'Invalid PE signature'
        machine, count = struct.unpack_from('<HH', data, pe+4)
        optional_size = struct.unpack_from('<H', data, pe+20)[0]
        assert machine == 0x8664, 'Expected x64 client'
        table = pe+24+optional_size
        sections = [struct.unpack_from('<IIII', data, table+40*i+8) for i in range(count)]

        def rva(offset):
            for virtual_size, virtual_address, raw_size, raw_address in sections:
                if raw_address <= offset < raw_address+raw_size:
                    return virtual_address+offset-raw_address
            raise AssertionError('Signature is outside mapped PE sections')

        for name, contract in baseline['hooks'].items():
            pattern = b''.join(b'.' if token == '?' else re.escape(bytes.fromhex(token))
                               for token in contract['pattern'].split())
            matches = [m.start() for m in re.finditer(pattern, data, re.DOTALL)]
            assert len(matches) == 1, f'Hook is not unique: {name}'
            assert rva(matches[0]) == int(contract['rva'], 16), f'Hook target drift: {name}'
    print(f"SDK baseline passed: ASA {baseline['game_version']}, CL {baseline['cl']}, "
          f"Steam {baseline['steam_build']}; {len(baseline['native_classes'])} classes / {checked} fields. "
          f"Installed EXE checked: {bool(executable)}.")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, help='Optional installed ArkAscended.exe to validate read-only.')
    check(parser.parse_args().exe)
