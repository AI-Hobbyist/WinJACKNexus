"""Navigate unresolved source-key proposals; this is not acceptance evidence."""
import csv
import json
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Docs/i18n'

def main():
    paths = list((ROOT/'locate').glob('*.json'))
    paths += list((ROOT/'src/native_fx/effects').glob('*/locate/*.json'))
    paths += list((ROOT/'Develop').glob('**/locate/*.json'))
    paths += list((ROOT/'src/vst/locate').glob('*.json'))
    paths += list((ROOT/'tools/vst_scanner/locate').glob('*.json'))
    actual = set()
    for path in paths:
        domains = json.loads(path.read_text(encoding='utf-8-sig')).get('domains', {})
        if not isinstance(domains, dict):
            continue
        for domain, values in domains.items():
            if isinstance(values, dict):
                actual.update((domain, key) for key in values)
    with (OUT/'S0-keys.csv').open(encoding='utf-8-sig', newline='') as file:
        rows = list(csv.DictReader(file))
    gaps = [row for row in rows if row.get('usage') != 'identity-guard'
            and (row['domain'], row['key']) not in actual]
    report = {'status': 'navigation gaps, not completion verification',
              'limits': ['Includes old proposed keys awaiting mapping and potentially untranslated displays.',
                         'Exact domain/key membership does not establish translation accuracy or UI coverage.'],
              'unmatched_occurrences': len(gaps),
              'domains': dict(Counter(row['domain'] for row in gaps)), 'rows': gaps}
    (OUT/'S6-coverage-gaps.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({'unmatched_occurrences': len(gaps), 'domains': report['domains']}, ensure_ascii=False))

if __name__ == '__main__':
    main()
