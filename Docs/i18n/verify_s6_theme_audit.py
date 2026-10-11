"""Verify the open Theme audit inventory, without granting UI acceptance."""

import csv
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Docs/i18n'


def main():
    audit = json.loads((OUT / 'S6-theme-source-audit.json').read_text(encoding='utf-8'))
    gaps = json.loads((OUT / 'S6-coverage-gaps.json').read_text(encoding='utf-8'))
    expected = [row for row in gaps['rows'] if row['domain'] == 'theme']
    assert audit['remaining_source_occurrences'] == len(expected) == 246
    assert len(audit['rows']) == len(expected)
    assert {row['source_id'] for row in audit['rows']} == {
        row['source_id'] for row in expected
    }
    assert all(
        row['status'] == 'REQUIRES_DISPLAY_AND_CALL_PATH_REVIEW'
        for row in audit['rows']
    )
    for item in audit['source_hashes']:
        assert (
            item['sha256']
            == hashlib.sha256((ROOT / item['file']).read_bytes()).hexdigest()
        )
    with (OUT / 'S0-exclusions.csv').open(encoding='utf-8-sig', newline='') as file:
        excluded = list(csv.DictReader(file))
    assert len(audit['excluded_temporary_carriers']) == 4
    assert all(row in excluded for row in audit['excluded_temporary_carriers'])
    s0 = json.loads((OUT / 'S0-verification.json').read_text(encoding='utf-8'))
    assert s0['result'] == 'PASS'
    result = {
        'stage': 'S6',
        'result': 'SOURCE_INVENTORY_PASS_THEME_OPEN',
        'theme_pending_occurrences': 246,
        'all_candidate_occurrences': 352,
        'ui_runs': 0,
        'module_builds': 0,
        'scope': 'Current source hashes and inventory membership only; no translation or runtime acceptance.',
    }
    (OUT / 'S6-theme-audit-verification.json').write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8'
    )
    print(
        'Theme audit inventory PASS: 246 open occurrences, four preserved carrier metadata strings; implementation/UI acceptance remain open.'
    )


if __name__ == '__main__':
    main()
