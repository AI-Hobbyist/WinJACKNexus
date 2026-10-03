"""Verify S3 catalogs, actual deployed bytes and recorded Windows UI runs."""
import hashlib
import csv
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Docs/i18n'
LANGUAGES = ('en-US', 'zh-CN', 'ja-JP', 'ko-KR')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    runtime = ROOT / 'build/release/bin'
    manifest = json.loads((ROOT / 'locate/manifest.json').read_text(encoding='utf-8'))
    records = json.loads((OUT / 'S3-strings.json').read_text(encoding='utf-8'))
    identities = {(row['domain'], row['key']) for row in records}
    assert len(identities) == len(records) == 132
    assert {row['domain'] for row in records} == {'adapter', 'patchbay'}
    assert identities == {(domain, key) for domain in ('adapter', 'patchbay')
                          for key in manifest['requiredKeys'][domain]}
    scope = {'src/adapter/src/Library.cpp', 'src/patchbay/mod/src/Library.cpp',
             'src/patchbay/mod/src/PatchbayCanvas.cpp',
             'src/patchbay/mod/include/mod_patchbay/LocalizedDialogs.hpp'}
    with (OUT / 'S0-keys.csv').open(encoding='utf-8-sig', newline='') as source:
        scoped = [row for row in csv.DictReader(source) if row['file'] in scope]
    assert scoped and {(row['domain'], row['key']) for row in scoped} <= identities
    s0 = json.loads((OUT / 'S0-verification.json').read_text(encoding='utf-8'))
    assert s0['result'] == 'PASS'
    audit = (ROOT / 'i18n翻译核对.md').read_text(encoding='utf-8-sig')
    packages = []
    for language in LANGUAGES:
        path = ROOT / 'locate' / f'{language}.json'
        catalog = json.loads(path.read_text(encoding='utf-8'))
        assert catalog['language'] == language
        assert catalog['moduleId'] == manifest['moduleId']
        assert catalog['catalogVersion'] == manifest['catalogVersion']
        for domain in ('adapter', 'patchbay'):
            assert set(catalog['domains'][domain]) == set(manifest['requiredKeys'][domain])
        for row in records:
            text = catalog['domains'][row['domain']][row['key']]
            assert text and text == row[language]
            parameters = lambda value: set(re.findall(r'\{([A-Za-z][A-Za-z0-9_]*)\}', value))
            assert parameters(text) == parameters(row['en-US'])
            matching = [line for line in audit.splitlines()
                        if line.startswith(f"| {row['domain']} | {row['key']} |")]
            assert len(matching) == 1
            assert text.replace('|', '&#124;').replace('\n', '<br>') in matching[0]
            assert '待母语/领域审校' in matching[0]
        assert path.read_bytes() == (runtime / 'locate' / path.name).read_bytes()
        packages.append({'language': language, 'keys': len(records), 'sha256': digest(path)})
    assert (runtime / 'locate/manifest.json').read_bytes() == (ROOT / 'locate/manifest.json').read_bytes()
    ctest = (ROOT / 'build/Develop/CTestTestfile.cmake').read_text(encoding='utf-8')
    assert 'patchbay_i18n_tests' in ctest and 'QT_QPA_PLATFORM=windows' in ctest
    assert 'QT_QPA_PLATFORM=offscreen' not in ctest
    windows = ['patchbay', 'rename', 'autosave', 'open', 'save', 'unsaved', 'devices', 'subgraph', 'context']
    windows += [f'{kind}-{index}' for kind in ('adapter', 'meter') for index in range(4)]
    renders = []
    runs = []
    for scale in ('1', '1.5', '2'):
        log = (OUT / f'S3-tests-scale-{scale}.log').read_text(encoding='utf-8-sig')
        result = re.search(r'S3 PASS: (\d+) real-window checks', log)
        font = re.search(r'SYSTEM_FONT (.+)', log)
        assert result and font
        assert 'S3 assertion failed:' not in log
        runs.append({'scale': scale, 'checks': int(result[1]), 'font': font[1].strip()})
        for language in LANGUAGES:
            for window in windows:
                path = OUT / 'S3-rendered' / f'scale-{scale}' / f'{window}-{language}.png'
                assert path.exists() and path.read_bytes().startswith(b'\x89PNG')
                renders.append(path)
    sources = ['Develop/PatchbayI18nTests.cpp', 'Develop/CMakeLists.txt',
               'src/patchbay/mod/src/Library.cpp', 'src/patchbay/mod/src/PatchbayCanvas.cpp',
               'src/patchbay/mod/include/mod_patchbay/LocalizedDialogs.hpp',
               'src/patchbay/mod/CMakeLists.txt', 'src/adapter/src/Library.cpp',
               'src/adapter/CMakeLists.txt', 'src/ui/include/WinJACKNexus/UI/LocalizedText.hpp',
               'src/ui/include/WinJACKNexus/UI/TrayController.hpp', 'src/ui/src/TrayController.cpp',
               'src/ui/CMakeLists.txt', 'Docs/i18n/S3-strings.json']
    artifacts = ['WinJACKNexus.exe', 'ui.dll', 'mod_patchbay.dll',
                 'wjn_patchbay_i18n_tests.exe']
    # Runtime module folders are discovered without assuming the package layout.
    adapter = list((runtime / 'nodes').rglob('mod_adapter.dll'))
    assert len(adapter) == 1
    artifacts = [runtime / item for item in artifacts]
    artifacts += adapter
    report = {'stage': 'S3', 'result': 'PASS', 'configuration': 'Release',
              'platform': 'windows', 'packages': packages, 'runs': runs,
              'source_scope_occurrences': len(scoped),
              'source_scope_keys': len({(row['domain'], row['key']) for row in scoped}),
              'render_hashes': {p.relative_to(ROOT).as_posix(): digest(p) for p in renders},
              'source_hashes': {p: digest(ROOT / p) for p in sources},
              'artifact_hashes': {p.relative_to(ROOT).as_posix(): digest(p) for p in artifacts},
              'limits': ['UI/model/module loading only; no JACK connection integration.',
                         'Draft translations await human language/domain review in S6.',
                         'This asset report does not replace visual inspection or S0 coverage reconciliation.']}
    (OUT / 'S3-verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(f'S3 assets PASS: {len(records)} keys in four deployed catalogs; {len(renders)} real-window captures.')


if __name__ == '__main__':
    main()
