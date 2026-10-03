"""Synchronize explicit S4 draft records; this is not a completion verifier."""
import csv
import json
from pathlib import Path
from scan_s0 import collect

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Docs/i18n'
LANGUAGES = ('zh-CN', 'en-US', 'ja-JP', 'ko-KR')
SOURCES = {
    'aux': ('src/ui/src/AuxSettingsWindow.cpp',),
    'mixer': ('src/ui/src/MixerWindow.cpp', 'src/ui/src/MixerOverview.hpp', 'src/ui/src/LocalizedMixerErrors.hpp'),
    'fxhost': ('src/ui/src/FxSettingsWindow.cpp', 'src/ui/src/FxLevelMeterWidget.cpp'),
    'ui': ('src/ui/src/HistoryChartWidget.cpp', 'src/ui/src/MetricStatisticsWidget.cpp'),
    'meterbridge': ('src/meter/mod/', 'src/meter/probe/Library.cpp'),
}


def main():
    with (OUT / 'S4-strings.tsv').open(encoding='utf-8', newline='') as source:
        records = list(csv.DictReader(source, delimiter='\t'))
    assert len(records) == len({(r['domain'], r['key']) for r in records})
    _, literals = collect()
    for record in records:
        assert all(record[l] for l in LANGUAGES)
        record['source'] = record['zh-CN']
        record['review'] = '待母语/领域审校'
        locations = sorted({f"{item['file']}:{item['line']}" for item in literals
                            if item['file'].startswith(SOURCES[record['domain']])
                            and item['literal'] == record['key']})
        assert locations, f"No actual key call site: {record['domain']}/{record['key']}"
        record['location'] = '；'.join(locations)
    (OUT / 'S4-strings.json').write_text(json.dumps(records, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    domains = sorted({r['domain'] for r in records})
    manifest_path = ROOT / 'locate/manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    manifest['catalogVersion'] = max(4, manifest['catalogVersion'])
    manifest['domains'] = list(dict.fromkeys(manifest['domains'] + domains))
    for domain in domains:
        stage_keys = [r['key'] for r in records if r['domain'] == domain]
        later_keys = [k for k in manifest['requiredKeys'].get(domain, []) if domain == 'ui' and k.startswith('clientName.')]
        manifest['requiredKeys'][domain] = stage_keys + later_keys
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    for language in LANGUAGES:
        path = ROOT / 'locate' / f'{language}.json'
        catalog = json.loads(path.read_text(encoding='utf-8'))
        catalog['catalogVersion'] = manifest['catalogVersion']
        for domain in domains:
            catalog['domains'][domain] = {r['key']: r[language] for r in records if r['domain'] == domain}
        path.write_text(json.dumps(catalog, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    path = ROOT / 'i18n翻译核对.md'
    results, exclusions = path.read_text(encoding='utf-8-sig').split('## 表二：已忽略翻译的部分', 1)
    results = '\n'.join(line for line in results.splitlines()
                        if not any(line.startswith('| '+d+' | ') for d in domains)) + '\n'
    def cell(value):
        return value.replace('|', '&#124;').replace('\n', '<br>')
    rows = ['| '+' | '.join([r['domain'], r['key'], cell(r['source']+'；'+r['location'])]
                           + [cell(r[l]) for l in LANGUAGES]
                           + [r['review']+'；S4 实施中，未验收', '2026-10-03'])+' |' for r in records]
    position = results.index('\n| host |')
    results = results[:position] + '\n' + '\n'.join(rows) + results[position:]
    path.write_text(results+'## 表二：已忽略翻译的部分'+exclusions, encoding='utf-8')
    print(f'S4 draft synced: {len(records)} keys; Release/UI/coverage verification remains pending.')


if __name__ == '__main__':
    main()
