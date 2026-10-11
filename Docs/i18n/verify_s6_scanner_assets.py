"""Check scanner draft catalogs and deployment; this does not verify its UI."""

import csv
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Docs/i18n'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    rows = list(
        csv.DictReader(
            (OUT / 'S6-scanner-strings.tsv').open(encoding='utf-8', newline=''),
            delimiter='\t',
        )
    )
    keys = {row['key'] for row in rows}
    assert len(rows) == len(keys) == 77
    source = ROOT / 'tools/vst_scanner/locate'
    runtime = ROOT / 'build/release/bin'
    deployed = runtime / 'vstscanner/locate'
    manifest = json.loads((source / 'manifest.json').read_text(encoding='utf-8'))
    assert set(manifest['requiredKeys']['vstscanner']) == keys
    assert manifest['moduleId'] == 'vst-scanner' and manifest['catalogVersion'] == 1
    paths = [source / 'manifest.json', deployed / 'manifest.json']
    assert paths[0].read_bytes() == paths[1].read_bytes()
    tokens = lambda value: set(re.findall(r'%\d+|\{\w+\}', value))
    audit = (ROOT / 'i18n翻译核对.md').read_text(encoding='utf-8-sig')
    for language in ('en-US', 'zh-CN', 'ja-JP', 'ko-KR'):
        path = source / (language + '.json')
        catalog = json.loads(path.read_text(encoding='utf-8'))
        assert (
            catalog['moduleId'] == manifest['moduleId']
            and catalog['language'] == language
        )
        assert catalog['catalogVersion'] == manifest['catalogVersion']
        assert set(catalog['domains']) == {'vstscanner'}
        assert set(catalog['domains']['vstscanner']) == keys
        assert path.read_bytes() == (deployed / path.name).read_bytes()
        for row in rows:
            value = catalog['domains']['vstscanner'][row['key']]
            assert value == row[language] and value.strip()
            assert tokens(value) == tokens(row['en-US'])
            assert '| vstscanner | ' + row['key'] + ' |' in audit
        paths += [path, deployed / path.name]
    source_files = [
        ROOT / 'tools/vst_scanner/src' / name
        for name in ('main.cpp', 'MainWindow.cpp', 'Scanner.cpp')
    ]
    calls = '\n'.join(path.read_text(encoding='utf-8') for path in source_files)
    used = set(re.findall(r'scannerText\("([^"\n]+)"', calls))
    assert used == keys, (used - keys, keys - used)
    # The translation callback is confined to display lookup; serialization and
    # category IDs remain in the core, and it has no UI-library dependency.
    core = source_files[-1].read_text(encoding='utf-8')
    assert 'WinJACKNexus/UI' not in core
    assert not re.search(r'QStringLiteral\("[^"\n]*[\u4e00-\u9fff]', core)
    probe = (ROOT / 'tools/vst_scanner/src/ProbeMain.cpp').read_text(encoding='utf-8')
    probe_keys = set(re.findall(r'"(probe\.[A-Za-z0-9]+)"', probe))
    assert probe_keys == {key for key in keys if key.startswith('probe.')}
    assert 'pathString(path), error.text)' in probe  # SDK diagnostic stays opaque.
    assert 'localizedProbeError(result, QStringLiteral("error"))' in core
    assert 'localizedProbeError(childResult, QStringLiteral("probe_error"))' in core
    assert (
        'verified = childResult.value(QStringLiteral("probe_error")).toString().isEmpty()'
        in core
    )
    protocol = json.loads(
        (OUT / 'S6-probe-verification.json').read_text(encoding='utf-8')
    )
    assert (
        protocol['result'] == 'PASS_LIMITED_FAILURE_PATH' and protocol['ui_runs'] == 0
    )
    for item in protocol['records']:
        assert item['runs'] == 1
        assert item['binary_sha256'] == sha(
            runtime / f"Vst Scanner Probe{item['bits']}.exe"
        )
    paths += source_files + [
        ROOT / 'tools/vst_scanner/include/WinJACKNexus/VstScanner' / name
        for name in ('Language.hpp', 'Scanner.hpp', 'MainWindow.hpp')
    ]
    paths += [
        ROOT / 'tools/vst_scanner/CMakeLists.txt',
        ROOT / 'tools/vst_scanner/tests/I18nTests.cpp',
        OUT / 'S6-scanner-strings.tsv',
        ROOT / 'tools/vst_scanner/src/ProbeMain.cpp',
        OUT / 'S6-probe-build.log',
        OUT / 'S6-probe-tests.log',
        OUT / 'S6-probe-verification.json',
        OUT / 'verify_s6_probe_protocol.py',
        runtime / 'Vst Scanner.exe',
        runtime / 'Vst Scanner Probe64.exe',
        runtime / 'Vst Scanner Probe32.exe',
        runtime / 'ui.dll',
        runtime / 'nodes/vst/mod_vst_host.dll',
        runtime / 'wjn_vst_scanner_i18n_tests.exe',
        OUT / 'S6-scanner-tests.log',
        OUT / 'S6-scanner-build.log',
        OUT / 'S6-scanner-build-repair.log',
    ]
    failure = (OUT / 'S6-scanner-tests.log').read_text(encoding='utf-8-sig')
    assert 'S6 scanner FAIL: locatePath must be locate' in failure
    report = {
        'stage': 'S6',
        'result': 'CATALOG_AND_DEPLOYMENT_PASS_UI_UNVERIFIED',
        'module': 'vst-scanner',
        'keys': len(keys),
        'languages': ['en-US', 'zh-CN', 'ja-JP', 'ko-KR'],
        'ui_test_runs': 1,
        'ui_test_exit_code': 2,
        'ui_window_created': False,
        'ui_status': 'First run failed before window creation. Registration path repaired and Release rebuilt; no repeated module test per user instruction.',
        'human_review': 'pending user review',
        'probe_protocol': 'Two architectures, one missing-library run each; other runtime branches and translated UI are unverified.',
        'ui_test_executable': 'Historical first-run fixture; not rebuilt or rerun for the additional probe keys.',
        'evidence': [
            {'file': str(path.relative_to(ROOT)), 'sha256': sha(path)} for path in paths
        ],
    }
    (OUT / 'S6-scanner-verification.json').write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8'
    )
    print(
        'S6 scanner catalogs/deployment PASS: 77 keys, four languages; UI UNVERIFIED (single run failed before window creation).'
    )


if __name__ == '__main__':
    main()
