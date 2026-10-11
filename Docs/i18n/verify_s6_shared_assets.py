"""Verify shared-control draft assets and Release deployment, without UI runs."""

import csv
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Docs/i18n'


def main():
    rows = list(
        csv.DictReader(
            (OUT / 'S6-shared-strings.tsv').open(encoding='utf-8', newline=''),
            delimiter='\t',
        )
    )
    assert len(rows) == 3
    keys = {row['key'] for row in rows}
    assert keys == {'choice.filterPlaceholder', 'dock.expand', 'dock.collapse'}
    source = ROOT / 'src/ui/src/Controls.cpp'
    code = source.read_text(encoding='utf-8')
    for row in rows:
        assert row['domain'] == 'ui'
        assert f'localizedText("ui", "{row["key"]}", "{row["en-US"]}")' in code
    assert not any(text in code for text in ('输入筛选', '展开侧边栏', '收缩侧边栏'))
    manifest_path = ROOT / 'locate/manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    assert manifest['catalogVersion'] == 6
    assert sum(map(len, manifest['requiredKeys'].values())) == 536
    assert keys <= set(manifest['requiredKeys']['ui'])
    runtime = ROOT / 'build/release/bin'
    paths = [
        source,
        manifest_path,
        OUT / 'S6-shared-strings.tsv',
        ROOT / 'Docs/UI-API.md',
        ROOT / 'Develop/NativeFx/NativeFxI18nTests.cpp',
        runtime / 'ui.dll',
        runtime / 'WinJACKNexus.exe',
        runtime / 'wjn_native_fx_i18n_tests.exe',
        OUT / 'S6-shared-build.log',
    ]
    assert manifest_path.read_bytes() == (runtime / 'locate/manifest.json').read_bytes()
    paths.append(runtime / 'locate/manifest.json')
    review = (ROOT / 'i18n翻译核对.md').read_text(encoding='utf-8')
    for language in ('en-US', 'zh-CN', 'ja-JP', 'ko-KR'):
        path = ROOT / 'locate' / f'{language}.json'
        deployed = runtime / 'locate' / path.name
        catalog = json.loads(path.read_text(encoding='utf-8'))
        assert catalog['catalogVersion'] == manifest['catalogVersion']
        assert catalog['language'] == language
        assert catalog['moduleId'] == manifest['moduleId']
        assert path.read_bytes() == deployed.read_bytes()
        for domain, required in manifest['requiredKeys'].items():
            assert set(catalog['domains'][domain]) == set(required)
        for row in rows:
            value = catalog['domains']['ui'][row['key']]
            assert value == row[language] and value.strip()
            assert re.findall(r'%\d+|\{\w+\}', value) == []
            assert '| ui | ' + row['key'] + ' |' in review
        paths += [path, deployed]
    qrc = ROOT / 'src/ui/resources/ui.qrc'
    assert '../../../locate/en-US.json' in qrc.read_text(encoding='utf-8')
    assert '../../../locate/manifest.json' in qrc.read_text(encoding='utf-8')
    paths.append(qrc)
    result = {
        'stage': 'S6',
        'result': 'ASSETS_AND_RELEASE_BUILD_PASS_UI_UNVERIFIED',
        'shared_keys': 3,
        'host_catalog_version': 6,
        'host_keys': 536,
        'module_test_runs': 0,
        'ui_shows': 0,
        'call_site_tests': 'Compiled successfully; not executed under single module test instruction.',
        'unverified': [
            'New placeholder and sidebar tooltip runtime display',
            'Draft/selection and caller-placeholder checks at runtime',
        ],
        'human_review': 'Pending user review',
        'evidence': [
            {
                'file': str(path.relative_to(ROOT)),
                'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            }
            for path in paths
        ],
    }
    (OUT / 'S6-shared-verification.json').write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8'
    )
    print(
        'S6 shared assets/build PASS: three keys, four languages, host v6/536; UI UNVERIFIED, no repeated module test.'
    )


if __name__ == '__main__':
    main()
