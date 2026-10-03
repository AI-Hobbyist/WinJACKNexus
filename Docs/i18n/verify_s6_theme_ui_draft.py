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
    display_rules = (ROOT/'src/ui/src/LocalizedThemeMessages.hpp').read_text(encoding='utf-8')
    for row in rows:
        assert json.dumps(row['key']) in code + display_rules
        assert json.dumps(row['en-US'], ensure_ascii=False) in code + display_rules
    assert 'manager->bind(this, [this] { refreshLocalizedIdentity(); refreshLocalizedDiagnostics(); });' in code
    assert 'const QSignalBlocker blocker(field);' in code
    manifest_path = ROOT/'locate/manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    assert manifest['catalogVersion'] == 10
    assert keys <= set(manifest['requiredKeys']['theme'])
    assert sum(map(len, manifest['requiredKeys'].values())) == 774
    paths = [source, table, manifest_path,
             ROOT/'src/ui/include/WinJACKNexus/UI/ThemeManagementDialog.hpp',
             ROOT/'tests/theme/ThemeManagementTests.cpp', ROOT/'Docs/UI-API.md']
    paths.append(ROOT/'src/ui/src/LocalizedThemeMessages.hpp')
    paths.append(ROOT/'src/ui/src/LocalizedThemeDialogs.hpp')
    assert code.count('ThemeDialogs::file(') == 4
    assert 'QFileDialog::getOpenFileName' not in code and 'QFileDialog::getSaveFileName' not in code
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
              'theme_ui_keys': 80, 'host_catalog_version': 10, 'host_keys': 774,
              'module_test_runs': 0, 'module_builds': 0, 'ui_shows': 0,
              'remaining': ['Diagnostic call-path reconciliation',
                            'Complete remaining canonical error mappings',
                            'File-dialog call-site runtime acceptance',
                            'Call-site checks, final Release deployment and one module UI run',
                            'Final S0 reconciliation and user human review'],
              'runtime': 'Existing v6 deployment has not been rebuilt for these v9 sources.',
              'evidence': [{'file': str(path.relative_to(ROOT)),
                            'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                           for path in paths]}
    (OUT/'S6-theme-ui-draft-verification.json').write_text(
        json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print('Theme UI source draft PASS: 80 keys, four languages, host v10/774. Build/deployment/UI acceptance pending remaining implementation.')


if __name__ == '__main__':
    main()
