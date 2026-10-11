"""Reconcile VST host catalogs and its one actual-DLL Windows test run."""

import csv
import hashlib
import json
import re
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Docs/i18n'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    rows = list(
        csv.DictReader(
            (OUT / 'S6-vst-strings.tsv').open(encoding='utf-8', newline=''),
            delimiter='\t',
        )
    )
    keys = {row['key'] for row in rows}
    assert len(keys) == len(rows) == 57
    source = ROOT / 'src/vst/locate'
    deployed = ROOT / 'build/release/bin/nodes/vst/locate'
    manifest = json.loads((source / 'manifest.json').read_text(encoding='utf-8'))
    assert (
        manifest['moduleId'] == 'module-winjacknexus-vst-host'
        and manifest['catalogVersion'] == 1
    )
    assert set(manifest['requiredKeys']['vst']) == keys and manifest['domains'] == [
        'vst'
    ]
    assert (source / 'manifest.json').read_bytes() == (
        deployed / 'manifest.json'
    ).read_bytes()
    evidence = [
        source / 'manifest.json',
        deployed / 'manifest.json',
        OUT / 'S6-vst-strings.tsv',
    ]
    audit = (ROOT / 'i18n翻译核对.md').read_text(encoding='utf-8-sig')
    tokens = lambda value: set(re.findall(r'%\d+|\{\w+\}', value))
    for language in ('en-US', 'zh-CN', 'ja-JP', 'ko-KR'):
        path = source / (language + '.json')
        catalog = json.loads(path.read_text(encoding='utf-8'))
        assert (
            catalog['moduleId'] == manifest['moduleId']
            and catalog['language'] == language
        )
        assert catalog['catalogVersion'] == manifest['catalogVersion']
        assert (
            set(catalog['domains']) == {'vst'}
            and set(catalog['domains']['vst']) == keys
        )
        assert path.read_bytes() == (deployed / path.name).read_bytes()
        for row in rows:
            value = catalog['domains']['vst'][row['key']]
            assert (
                value == row[language]
                and value.strip()
                and tokens(value) == tokens(row['en-US'])
            )
            assert '| vst | ' + row['key'] + ' |' in audit
        for key in ('filter.plugin', 'filter.preset'):
            assert re.findall(r'\*\.\w+', catalog['domains']['vst'][key]) == re.findall(
                r'\*\.\w+', next(row['en-US'] for row in rows if row['key'] == key)
            )
        evidence += [path, deployed / path.name]
    cpp = ROOT / 'src/vst/src/Library.cpp'
    calls = dict(
        re.findall(
            r'vstText\("([^"\n]+)", "((?:\\.|[^"\\])*)"\)',
            cpp.read_text(encoding='utf-8'),
        )
    )
    assert set(calls) == keys - {'module.name'}
    assert all(
        calls[row['key']] == row['en-US'] for row in rows if row['key'] != 'module.name'
    )
    log = OUT / 'S6-vst-tests.log'
    match = re.search(
        r'S6 VST host PASS: (\d+) checks; platform=windows font=(.+?); popup shows=(\d+) / 100; single module run, native owned capture, no scale matrix',
        log.read_text(encoding='utf-8-sig'),
    )
    assert match and int(match[1]) == 77 and 0 < int(match[3]) <= 100
    image = OUT / 'S6-vst-ko-KR.png'
    with Image.open(image) as bitmap:
        assert bitmap.width > 0 and bitmap.height > 0
        assert any(low != high for low, high in bitmap.convert('RGB').getextrema())
    evidence += [
        cpp,
        ROOT / 'src/vst/src/I18n.cpp',
        ROOT / 'src/vst/src/I18n.hpp',
        ROOT / 'src/vst/CMakeLists.txt',
        ROOT / 'Develop/VstI18nTests.cpp',
        ROOT / 'build/release/bin/nodes/vst/mod_vst_host.dll',
        ROOT / 'build/release/bin/wjn_vst_i18n_tests.exe',
        ROOT / 'build/release/bin/ui.dll',
        log,
        image,
        OUT / 'S6-vst-build.log',
        OUT / 'S6-vst-build-repair.log',
    ]
    report = {
        'stage': 'S6',
        'module': 'VST host',
        'result': 'PASS',
        'keys': 57,
        'checks': int(match[1]),
        'module_test_runs': 1,
        'popup_shows': int(match[3]),
        'platform': 'windows',
        'font': match[2],
        'scope': 'Actual DLL settings window, four-language hot text/status, drafts/selection/focus, stable enum data and serialized bytes, dual leases, detach fallback and unload; catalog/deployment consistency.',
        'not_verified': [
            'VSTProcess/plugin loading or JACK integration',
            'Modal file and class-choice dialog interaction',
            'Additional cold-start and scale matrices',
        ],
        'human_review': 'pending user review; automated and image checks do not approve translations',
        'evidence': [
            {'file': str(path.relative_to(ROOT)), 'sha256': sha(path)}
            for path in evidence
        ],
    }
    (OUT / 'S6-vst-verification.json').write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8'
    )
    print(
        'S6 VST host assets PASS: 57 keys, four languages, 77 actual-DLL Windows checks, one show/run; human review pending.'
    )


if __name__ == '__main__':
    main()
