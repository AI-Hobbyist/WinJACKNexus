"""Check package/namespace display mappings without running a module test."""
import csv
import hashlib
import json
import re
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Docs/i18n'


def main():
    table = OUT/'S6-theme-package-namespace-strings.tsv'
    with table.open(encoding='utf-8', newline='') as file:
        rows = list(csv.DictReader(file, delimiter='\t'))
    assert len(rows) == len({row['key'] for row in rows}) == 109
    assert Counter(row['source_file'] for row in rows) == {
        'src/ui/src/ThemePackage.cpp': 69,
        'src/ui/src/ThemeNamespaceRegistry.cpp': 40}
    audit = json.loads((OUT/'S6-theme-source-audit.json').read_text(encoding='utf-8'))
    source_hashes = {item['file']: item['sha256'] for item in audit['source_hashes']}
    header = ROOT/'src/ui/src/LocalizedThemeMessages.hpp'
    rules = header.read_text(encoding='utf-8')
    paths = [table, header, ROOT/'tests/theme/ThemeManagementTests.cpp',
             ROOT/'src/ui/src/ThemeManagementDialog.cpp', ROOT/'Docs/UI-API.md']
    for file in {row['source_file'] for row in rows}:
        path = ROOT/file
        assert hashlib.sha256(path.read_bytes()).hexdigest() == source_hashes[file]
        paths.append(path)
    for row in rows:
        code = (ROOT/row['source_file']).read_text(encoding='utf-8')
        assert row['canonical'].count('%1') <= 1
        assert json.dumps(row['canonical'].replace('%1', ''), ensure_ascii=False) in code
        declaration = '{ '+', '.join(json.dumps(row[key], ensure_ascii=False)
                                    for key in ('key', 'canonical', 'en-US'))+' }'
        assert declaration in rules
    # Every candidate occurrence in these two files has a mapped canonical
    # template, including repeated null-result/invalid-target guards.
    candidates = [row for row in audit['rows'] if row['file'] in source_hashes
                  and row['file'] in {item['source_file'] for item in rows}]
    assert len(candidates) == 115
    for candidate in candidates:
        assert any(candidate['file'] == row['source_file']
                   and candidate['source'] == row['canonical'].replace('%1', '')
                   for row in rows), candidate
    manifest_path = ROOT/'locate/manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    assert manifest['catalogVersion'] == 10
    assert sum(map(len, manifest['requiredKeys'].values())) == 774
    assert len(manifest['requiredKeys']['theme']) == 238
    paths.append(manifest_path)
    tokens = lambda text: sorted(re.findall(r'\{\w+\}', text))
    for language in ('en-US', 'zh-CN', 'ja-JP', 'ko-KR'):
        path = ROOT/'locate'/f'{language}.json'
        catalog = json.loads(path.read_text(encoding='utf-8'))
        assert catalog['catalogVersion'] == 10 and catalog['language'] == language
        for domain, required in manifest['requiredKeys'].items():
            assert set(catalog['domains'][domain]) == set(required)
        for row in rows:
            value = catalog['domains']['theme'][row['key']]
            assert value == row[language] and value.strip()
            assert tokens(value) == tokens(row['en-US'])
        paths.append(path)
    result = {'stage': 'S6', 'result': 'THEME_PACKAGE_NAMESPACE_SOURCE_PASS_RUNTIME_EVIDENCE_SEPARATE',
              'display_templates': 109, 'mapped_original_occurrences': 115,
              'package_and_registry_sources_unchanged': True,
              'host_catalog_version': 10, 'host_keys': 774, 'theme_keys': 238,
              'module_builds_added': 0, 'module_test_runs_added': 0, 'ui_shows_added': 0,
              'remaining': ['Final S0 reconciliation and user human review'],
              'runtime_evidence': 'Docs/i18n/S6-theme-verification.json',
              'evidence': [{'file': str(path.relative_to(ROOT)),
                            'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                           for path in paths]}
    (OUT/'S6-theme-package-namespace-draft-verification.json').write_text(
        json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print('Theme package/namespace source PASS: 109 templates map 115 occurrences, canonical sources unchanged. Runtime evidence is separate in S6-theme-verification.json.')


if __name__ == '__main__':
    main()
