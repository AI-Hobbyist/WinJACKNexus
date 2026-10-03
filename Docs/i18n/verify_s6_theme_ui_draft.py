"""Check in-progress Theme UI sources/catalogs; not build or UI evidence."""
import csv
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Docs/i18n'


def main():
    table = OUT/'S6-theme-ui-strings.tsv'
    with table.open(encoding='utf-8', newline='') as file:
        rows = list(csv.DictReader(file, delimiter='\t'))
    keys = {row['key'] for row in rows}
    assert len(rows) == len(keys) == 80
    source = ROOT/'src/ui/src/ThemeManagementDialog.cpp'
    code = source.read_text(encoding='utf-8')
    for row in rows:
        assert json.dumps(row['key']) in code
        assert json.dumps(row['en-US'], ensure_ascii=False) in code
    assert 'manager->bind(this, [this] { refreshLocalizedIdentity(); });' in code
    assert 'const QSignalBlocker blocker(field);' in code
    manifest_path = ROOT/'locate/manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    assert manifest['catalogVersion'] == 7
    assert keys == set(manifest['requiredKeys']['theme'])
    assert sum(map(len, manifest['requiredKeys'].values())) == 616
    paths = [source, table, manifest_path,
             ROOT/'src/ui/include/WinJACKNexus/UI/ThemeManagementDialog.hpp',
             ROOT/'tests/theme/ThemeManagementTests.cpp', ROOT/'Docs/UI-API.md']
    tokens = lambda text: sorted(re.findall(r'%\d+|\{\w+\}', text))
    review = (ROOT/'i18n翻译核对.md').read_text(encoding='utf-8')
    for language in ('en-US', 'zh-CN', 'ja-JP', 'ko-KR'):
        path = ROOT/'locate'/f'{language}.json'
        catalog = json.loads(path.read_text(encoding='utf-8'))
        assert catalog['catalogVersion'] == manifest['catalogVersion']
        assert catalog['moduleId'] == manifest['moduleId']
        assert catalog['language'] == language
        for domain, required in manifest['requiredKeys'].items():
            assert set(catalog['domains'][domain]) == set(required)
        for row in rows:
            value = catalog['domains']['theme'][row['key']]
            assert value == row[language] and value.strip()
            assert tokens(value) == tokens(row['en-US'])
            assert '| theme | '+row['key']+' |' in review
        paths.append(path)
    result = {'stage': 'S6', 'result': 'THEME_UI_SOURCE_DRAFT_PASS_IMPLEMENTATION_OPEN',
              'theme_ui_keys': 80, 'host_catalog_version': 7, 'host_keys': 616,
              'module_test_runs': 0, 'module_builds': 0, 'ui_shows': 0,
              'remaining': ['Theme core/package/registry/catalog diagnostic display',
                            'Returned-error status refresh after language change',
                            'Open file-dialog title/filter refresh',
                            'Call-site checks, final Release deployment and one module UI run',
                            'Final S0 reconciliation and user human review'],
              'runtime': 'Existing v6 deployment has not been rebuilt for these v7 sources.',
              'evidence': [{'file': str(path.relative_to(ROOT)),
                            'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                           for path in paths]}
    (OUT/'S6-theme-ui-draft-verification.json').write_text(
        json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print('Theme UI source draft PASS: 80 keys, four languages, host v7/616. Build/deployment/UI acceptance pending remaining implementation.')


if __name__ == '__main__':
    main()
