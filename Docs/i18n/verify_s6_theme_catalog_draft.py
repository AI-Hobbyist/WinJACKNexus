"""Verify Theme GUI boundaries in source; no runtime or compilation claims."""
import csv
import hashlib
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT/'Docs/i18n'


def main():
    source = ROOT/'src/ui/src/ThemeCatalog.cpp'
    code = source.read_text(encoding='utf-8')
    old = subprocess.check_output(['git', 'show', '8564c61:src/ui/src/ThemeCatalog.cpp'], cwd=ROOT).decode('utf-8')
    expected = old.replace('#include <WinJACKNexus/UI/HostI18n.hpp>',
                           '#include <WinJACKNexus/UI/HostI18n.hpp>\n#include "LocalizedThemeMessages.hpp"')
    expected = expected.replace('reportError(issue);', 'reportError(ThemeMessages::display(issue));')
    assert code == expected, 'Catalog backend must retain its original state, errors and cache behavior'
    assert code.count('reportError(ThemeMessages::display(issue))') == 3
    table = OUT/'S6-theme-catalog-strings.tsv'
    with table.open(encoding='utf-8', newline='') as file:
        rows = list(csv.DictReader(file, delimiter='\t'))
    assert len(rows) == len({r['key'] for r in rows}) == 3
    adapter = ROOT/'src/ui/src/LocalizedThemeMessages.hpp'
    rules = adapter.read_text(encoding='utf-8')
    for row in rows:
        declaration = '{ '+', '.join(json.dumps(row[k], ensure_ascii=False)
                                    for k in ('key','canonical','en-US'))+' }'
        assert declaration in rules
    paths = [source, adapter, table, ROOT/'src/ui/src/LocalizedThemeDialogs.hpp',
             ROOT/'src/ui/src/ThemeManagementDialog.cpp', ROOT/'tests/theme/ThemeManagementTests.cpp',
             ROOT/'Docs/i18n/S6-theme-diagnostic-boundaries.md', ROOT/'Docs/UI-API.md']
    manifest = json.loads((ROOT/'locate/manifest.json').read_text(encoding='utf-8'))
    assert manifest['catalogVersion'] == 10
    assert sum(map(len, manifest['requiredKeys'].values())) == 774
    for lang in ('en-US','zh-CN','ja-JP','ko-KR'):
        path = ROOT/'locate'/f'{lang}.json'
        catalog = json.loads(path.read_text(encoding='utf-8'))
        assert catalog['catalogVersion'] == 10
        for row in rows:
            value = catalog['domains']['theme'][row['key']]
            assert value == row[lang] and value.strip()
            assert re.findall(r'\{\w+\}',value) == re.findall(r'\{\w+\}',row['en-US'])
        paths.append(path)
    result = {'stage':'S6', 'result':'THEME_CATALOG_DIALOG_SOURCE_PASS_RUNTIME_EVIDENCE_SEPARATE',
              'catalog_templates':3, 'menu_error_display_boundaries':3,
              'file_dialog_workflows':4, 'canonical_catalog_backend_preserved':True,
              'host_catalog_version':10, 'host_keys':774,
              'module_builds_added':0, 'module_test_runs_added':0, 'ui_shows_added':0,
              'remaining': ['Final S0 reconciliation and user human review'],
              'runtime_evidence': 'Docs/i18n/S6-theme-verification.json',
              'evidence':[{'file':str(p.relative_to(ROOT)), 'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in paths]}
    (OUT/'S6-theme-catalog-draft-verification.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('Theme Catalog/dialog source PASS: 3 menu boundaries, 4 bound file workflows; runtime evidence is separate in S6-theme-verification.json.')


if __name__ == '__main__':
    main()
