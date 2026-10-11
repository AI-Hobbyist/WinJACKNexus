"""Verify S4 source coverage, deployed catalogs and actual Windows run artifacts.

This checks evidence consistency; visual inspection and human translation review
remain separate requirements.
"""

import csv
import hashlib
import json
import re
from pathlib import Path
from sync_s4_catalogs import SOURCES

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Docs/i18n'
LANGUAGES = ('en-US', 'zh-CN', 'ja-JP', 'ko-KR')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    runtime = ROOT / 'build/release/bin'
    manifest = json.loads((ROOT / 'locate/manifest.json').read_text(encoding='utf-8'))
    records = json.loads((OUT / 'S4-strings.json').read_text(encoding='utf-8'))
    identities = {(r['domain'], r['key']) for r in records}
    assert len(identities) == len(records) == 312
    assert set(SOURCES) == {r['domain'] for r in records}
    manifest_identities = {(d, k) for d in SOURCES for k in manifest['requiredKeys'][d]}
    # S4 owns its 312 records; S5 adds shared client-name chrome to the ui domain.
    assert identities <= manifest_identities
    assert all(
        d == 'ui' and k.startswith('clientName.')
        for d, k in manifest_identities - identities
    )
    placeholders = lambda value: set(
        re.findall(r'%(?:L?\d+|n)|\{[A-Za-z_]\w*\}', value)
    )
    audit = (ROOT / 'i18n翻译核对.md').read_text(encoding='utf-8-sig')
    for language in LANGUAGES:
        path = ROOT / 'locate' / f'{language}.json'
        catalog = json.loads(path.read_text(encoding='utf-8'))
        assert catalog['catalogVersion'] == manifest['catalogVersion']
        assert (
            catalog['moduleId'] == manifest['moduleId']
            and catalog['language'] == language
        )
        for domain in SOURCES:
            assert set(catalog['domains'][domain]) == set(
                manifest['requiredKeys'][domain]
            )
        for row in records:
            text = catalog['domains'][row['domain']][row['key']]
            assert text == row[language] and text.strip()
            assert placeholders(text) == placeholders(row['en-US'])
            lines = [
                line
                for line in audit.splitlines()
                if line.startswith(f"| {row['domain']} | {row['key']} |")
            ]
            assert (
                len(lines) == 1
                and text.replace('|', '&#124;').replace('\n', '<br>') in lines[0]
            )
        assert path.read_bytes() == (runtime / 'locate' / path.name).read_bytes()
    assert (ROOT / 'locate/manifest.json').read_bytes() == (
        runtime / 'locate/manifest.json'
    ).read_bytes()
    with (OUT / 'S0-keys.csv').open(encoding='utf-8-sig', newline='') as file:
        scoped = [
            r
            for r in csv.DictReader(file)
            if r['file'].startswith(
                tuple(p for paths in SOURCES.values() for p in paths)
            )
        ]
    assert scoped and all((r['domain'], r['key']) in identities for r in scoped), [
        (r['file'], r['line'], r['key'])
        for r in scoped
        if (r['domain'], r['key']) not in identities
    ]
    with (OUT / 'S0-pending.csv').open(encoding='utf-8-sig', newline='') as file:
        assert not list(csv.DictReader(file))
    windows = [
        'history',
        'mixer',
        'sel',
        'overview',
        'aux',
        'submix',
        'fx',
        'card',
        'bridge',
        'bridge-empty',
        'fx-preset-hot',
        'reference-hot',
        'clear-hot',
        'png-chooser-hot',
        'mixer-error-hot',
    ]
    windows += [
        f'{prefix}-{index}' for prefix in ('probe', 'probe-hot') for index in range(5)
    ]
    windows += [
        f'cold-{window}'
        for window in (
            'history',
            'mixer',
            'sel',
            'overview',
            'aux',
            'submix',
            'card',
            'bridge',
            'bridge-empty',
            'fx',
        )
    ]
    renders, runs = [], []
    for scale in ('1', '1.5', '2'):
        log = (OUT / f'S4-tests-scale-{scale}.log').read_text(encoding='utf-8-sig')
        result = re.search(
            r'S4 suite: (\d+) checks; system font=(.+); scale=([^\r\n]+) PASS', log
        )
        assert result and result[3] == scale and 'S4 check failed:' not in log
        for language in LANGUAGES:
            assert re.search(rf'S4 cold {language}: \d+ checks PASS', log)
            for window in windows:
                path = (
                    OUT / 'S4-rendered' / f'scale-{scale}' / f'{window}-{language}.png'
                )
                assert path.read_bytes().startswith(b'\x89PNG')
                renders.append(path)
        for path in sorted(
            (OUT / 'S4-rendered' / f'scale-{scale}').glob('*-tile-*.png')
        ):
            assert path.read_bytes().startswith(b'\x89PNG')
            renders.append(path)
        runs.append({'scale': scale, 'checks': int(result[1]), 'font': result[2]})
    sources = [
        'Develop/MixerMeterI18nTests.cpp',
        'Develop/CMakeLists.txt',
        'Docs/UI-API.md',
        'Docs/i18n/S4-strings.json',
    ]
    source_manifest = json.loads(
        (OUT / 'S0-source-manifest.json').read_text(encoding='utf-8')
    )
    sources += [
        r['file']
        for r in source_manifest['files']
        if r['file'].startswith(('src/meter/', 'src/ui/'))
    ]
    artifacts = [
        'WinJACKNexus.exe',
        'ui.dll',
        'mod_meterbrigde.dll',
        'wjn_mixer_meter_i18n_tests.exe',
        'nodes/meter/mod_meterbridge_probe.dll',
    ]
    report = {
        'stage': 'S4',
        'result': 'PASS',
        'configuration': 'Release',
        'platform': 'windows',
        'keys': len(records),
        'runs': runs,
        'source_scope_occurrences': len(scoped),
        'source_hashes': {name: digest(ROOT / name) for name in sources},
        'artifact_hashes': {
            (runtime / name).relative_to(ROOT).as_posix(): digest(runtime / name)
            for name in artifacts
        },
        'render_hashes': {p.relative_to(ROOT).as_posix(): digest(p) for p in renders},
        'limits': [
            'Evidence consistency only; does not replace screenshot inspection.',
            'Draft translations await human language/domain review in S6.',
            'No JACK initialization or audio connection tests.',
        ],
    }
    (OUT / 'S4-verification.json').write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8'
    )
    print(
        f'S4 assets PASS: {len(records)} keys, {len(scoped)} source occurrences, {len(renders)} actual Windows captures.'
    )


if __name__ == '__main__':
    main()
