"""Check ThemeCore display mappings; runtime acceptance remains pending."""
import csv
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Docs/i18n'


def main():
    table = OUT/'S6-theme-core-strings.tsv'
    with table.open(encoding='utf-8', newline='') as file:
        rows = list(csv.DictReader(file, delimiter='\t'))
    assert len(rows) == len({row['key'] for row in rows}) == 46
    core = ROOT/'src/ui/src/ThemeCore.cpp'
    source = core.read_text(encoding='utf-8')
    old_audit = json.loads((OUT/'S6-theme-source-audit.json').read_text(encoding='utf-8'))
    expected = next(item['sha256'] for item in old_audit['source_hashes']
                    if item['file'] == 'src/ui/src/ThemeCore.cpp')
    assert hashlib.sha256(core.read_bytes()).hexdigest() == expected
    header = ROOT/'src/ui/src/LocalizedThemeMessages.hpp'
    rules = header.read_text(encoding='utf-8')
    tokens = lambda text: sorted(re.findall(r'\{\w+\}', text))
    for row in rows:
        assert row['source_file'] == 'src/ui/src/ThemeCore.cpp'
        assert row['canonical'].count('%1') <= 1
        assert json.dumps(row['canonical'].replace('%1', ''), ensure_ascii=False) in source
        declaration = '{ '+', '.join(json.dumps(row[key], ensure_ascii=False)
                                    for key in ('key', 'canonical', 'en-US'))+' }'
        assert declaration in rules
    manifest_path = ROOT/'locate/manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    assert manifest['catalogVersion'] == 10
    assert sum(map(len, manifest['requiredKeys'].values())) == 774
    paths = [core, header, table, manifest_path,
             ROOT/'src/ui/src/ThemeManagementDialog.cpp',
             ROOT/'tests/theme/ThemeManagementTests.cpp', ROOT/'Docs/UI-API.md']
    for language in ('en-US', 'zh-CN', 'ja-JP', 'ko-KR'):
        path = ROOT/'locate'/f'{language}.json'
        catalog = json.loads(path.read_text(encoding='utf-8'))
        assert catalog['catalogVersion'] == 10
        for row in rows:
            value = catalog['domains']['theme'][row['key']]
            assert value == row[language] and value.strip()
            assert tokens(value) == tokens(row['en-US'])
        paths.append(path)
    result = {'stage': 'S6', 'result': 'THEME_CORE_DISPLAY_SOURCE_PASS_RUNTIME_PENDING',
              'core_templates': 46, 'core_source_unchanged': True,
              'host_catalog_version': 10, 'host_keys': 774,
              'module_builds': 0, 'module_test_runs': 0, 'ui_shows': 0,
              'remaining': ['Diagnostic call-path reconciliation',
                            'File-dialog call-site runtime acceptance', 'Final build/deployment/one real-window run',
                            'S0 refresh and user human review'],
              'evidence': [{'file': str(path.relative_to(ROOT)),
                            'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                           for path in paths]}
    (OUT/'S6-theme-core-draft-verification.json').write_text(
        json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print('ThemeCore display source PASS: 46 templates, canonical parser/diagnostic source unchanged; runtime/build acceptance pending.')


if __name__ == '__main__':
    main()
