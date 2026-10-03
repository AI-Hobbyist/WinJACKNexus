"""Generate Native Fx draft catalogs from reviewed explicit records, not approval."""
import csv
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Docs/i18n'
LANGUAGES = ('en-US', 'zh-CN', 'ja-JP', 'ko-KR')

def main():
    with (OUT / 'S5-strings.tsv').open(encoding='utf-8', newline='') as f:
        rows = list(csv.DictReader(f, delimiter='\t'))
    assert len(rows) == len({(r['effect'], r['key']) for r in rows})
    placeholders = lambda s: set(re.findall(r'%(?:L?\d+|n)|\{[A-Za-z_]\w*\}', s))
    for r in rows:
        assert all(r[l].strip() for l in LANGUAGES), r
        assert all(placeholders(r[l]) == placeholders(r['en-US']) for l in LANGUAGES), r
    for effect in ('gate', 'graphic_eq', 'deesser', 'compressor', 'limiter', 'delay', 'parametric_eq', 'reverb'):
        base = ROOT / 'src/native_fx/effects' / effect
        module_id = re.search(r'activeMeta\s*\{\s*"([^"]+)"', (base / 'Meta.hpp').read_text(encoding='utf-8'))[1]
        domain = 'nativefx.' + module_id
        selected = [r for r in rows if r['effect'] == effect]
        assert {'module.name', 'module.description'} <= {r['key'] for r in selected}
        locate = base / 'locate'
        locate.mkdir(exist_ok=True)
        manifest = {'formatVersion': 1, 'moduleId': module_id, 'domains': [domain],
                    'defaultLanguage': 'en-US', 'catalogVersion': 1,
                    'languages': {l: l + '.json' for l in LANGUAGES},
                    'requiredKeys': {domain: [r['key'] for r in selected]}}
        def save(name, value):
            (locate / name).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        save('manifest.json', manifest)
        for language in LANGUAGES:
            save(language + '.json', {'formatVersion': 1, 'catalogVersion': 1, 'moduleId': module_id,
                 'language': language, 'domains': {domain: {r['key']: r[language] for r in selected}}})
    (OUT / 'S5-strings.json').write_text(json.dumps(rows, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'S5 draft catalogs synchronized: {len(rows)} keys; implementation/Release/UI review pending.')

if __name__ == '__main__':
    main()
