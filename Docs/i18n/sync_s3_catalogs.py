"""Sync the reviewed S3 draft records into host catalogs and the audit table."""

import json
import sys
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LANGUAGES = ('zh-CN', 'en-US', 'ja-JP', 'ko-KR')


def main():
    verified = '--verified' in sys.argv
    if verified:
        evidence = json.loads(
            (ROOT / 'Docs/i18n/S3-verification.json').read_text(encoding='utf-8')
        )
        assert evidence['result'] == 'PASS'
        for group in ('source_hashes', 'artifact_hashes', 'render_hashes'):
            for name, expected in evidence[group].items():
                assert (
                    hashlib.sha256((ROOT / name).read_bytes()).hexdigest() == expected
                ), f'Stale S3 evidence: {name}'
        assert len(evidence['runs']) == 3 and all(
            run['checks'] >= 8037 for run in evidence['runs']
        )
    records = json.loads(
        (ROOT / 'Docs/i18n/S3-strings.json').read_text(encoding='utf-8')
    )
    assert len(records) == len({(r['domain'], r['key']) for r in records})
    domains = sorted({r['domain'] for r in records})
    assert set(domains) == {'patchbay', 'adapter'}
    from scan_s0 import collect

    _, literals = collect()
    for record in records:
        english = (
            record['en-US']
            .replace('\\', '\\\\')
            .replace('"', '\\"')
            .replace('\n', '\\n')
        )
        locations = sorted(
            {
                f"{item['file']}:{item['line']}"
                for item in literals
                if item['literal'] in (record['key'], english)
                and item['file'].startswith(('src/adapter/', 'src/patchbay/'))
            }
        )
        if locations:
            record['location'] = '；'.join(locations)
        assert record.get(
            'location'
        ), f"Missing source evidence for {record['domain']}/{record['key']}"
    (ROOT / 'Docs/i18n/S3-strings.json').write_text(
        json.dumps(records, ensure_ascii=False, indent=2) + '\n', encoding='utf-8'
    )
    path = ROOT / 'locate/manifest.json'
    manifest = json.loads(path.read_text(encoding='utf-8'))
    manifest['catalogVersion'] = 3
    manifest['domains'] = ['host'] + domains
    for domain in domains:
        manifest['requiredKeys'][domain] = [
            r['key'] for r in records if r['domain'] == domain
        ]
    path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8'
    )
    for language in LANGUAGES:
        path = ROOT / 'locate' / f'{language}.json'
        catalog = json.loads(path.read_text(encoding='utf-8'))
        catalog['catalogVersion'] = manifest['catalogVersion']
        for domain in domains:
            catalog['domains'][domain] = {
                r['key']: r[language] for r in records if r['domain'] == domain
            }
        path.write_text(
            json.dumps(catalog, ensure_ascii=False, indent=2) + '\n', encoding='utf-8'
        )
    path = ROOT / 'i18n翻译核对.md'
    audit = path.read_text(encoding='utf-8-sig')
    results, exclusions = audit.split('## 表二：已忽略翻译的部分', 1)
    audit = (
        '\n'.join(
            line
            for line in results.splitlines()
            if not any(line.startswith('| ' + domain + ' | ') for domain in domains)
        )
        + '\n'
    )

    def cell(value):
        return value.replace('|', '&#124;').replace('\n', '<br>')

    rows = []
    for r in records:
        source = (
            r['source']
            + '；'
            + r.get(
                'location',
                'src/'
                + (
                    'adapter/src/Library.cpp'
                    if r['domain'] == 'adapter'
                    else 'patchbay/mod/src（S3）'
                ),
            )
        )
        validation = (
            'S3 Release / 系统字体 Windows 实窗三档缩放验证通过'
            if verified
            else 'S3 验证进行中'
        )
        rows.append(
            '| '
            + ' | '.join(
                [r['domain'], r['key'], cell(source)]
                + [cell(r[l]) for l in LANGUAGES]
                + [r['review'] + '；' + validation, '2026-10-03']
            )
            + ' |'
        )
    # Insert into the translation results table; the separate exclusions table is preserved.
    position = audit.index('\n| host |')
    audit = audit[:position] + '\n' + '\n'.join(rows) + audit[position:]
    path.write_text(audit + '## 表二：已忽略翻译的部分' + exclusions, encoding='utf-8')
    print(
        f'S3 synced {len(records)} records across four catalogs and the audit; verified={verified}; human review remains pending.'
    )


if __name__ == '__main__':
    main()
