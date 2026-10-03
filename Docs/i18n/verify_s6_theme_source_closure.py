"""Source/key closure for Theme only; does not accept the remaining S6 scope."""
import csv
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Docs/i18n'


def main():
    def read(name):
        with (OUT/name).open(encoding='utf-8-sig',newline='') as file:
            return list(csv.DictReader(file))
    manifest=json.loads((ROOT/'locate/manifest.json').read_text(encoding='utf-8'))
    required=set(manifest['requiredKeys']['theme'])
    assert len(required)==238
    keys=read('S0-keys.csv')
    catalog=read('S0-key-catalog.csv')
    assert {r['key'] for r in keys if r['domain']=='theme'}==required
    assert {r['key'] for r in catalog if r['domain']=='theme'}==required
    gaps=json.loads((OUT/'S6-coverage-gaps.json').read_text(encoding='utf-8'))
    assert gaps['unmatched_occurrences']==len(gaps['rows']) and not any(r['domain']=='theme' for r in gaps['rows'])
    review=(ROOT/'i18n翻译核对.md').read_text(encoding='utf-8')
    translated,excluded=review.split('## 表二：已忽略翻译的部分',1)
    for key in required: assert '| theme | '+key+' |' in translated,key
    assert '| S6 Theme canonical |' in excluded
    source_report=json.loads((OUT/'S0-verification.json').read_text(encoding='utf-8'))
    assert source_report['result']=='PASS' and source_report['source_files']==277
    assert source_report['source_occurrences']==sum(int(r['literal_occurrences']) for r in read('S0-file-coverage.csv'))
    paths=[OUT/'S0-verification.json',OUT/'S0-classified-literals.csv',OUT/'S0-keys.csv',
           OUT/'S0-exclusions.csv',OUT/'S0-key-catalog.csv',OUT/'S0-resource-fields.csv',
           OUT/'S6-coverage-gaps.json',OUT/'classify_s0.py',ROOT/'i18n翻译核对.md',
           OUT/'S6-theme-diagnostic-boundaries.md']
    result={'stage':'S6','result':'THEME_SOURCE_KEY_AND_DUAL_TABLE_CLOSURE_PASS_S6_OPEN',
            'theme_display_keys':238,'theme_unmatched_occurrences':0,
            'remaining_other_occurrences':gaps['unmatched_occurrences'],'remaining_domains':gaps['domains'],
            'source_snapshot':'S0-source-manifest.json (current supplemental source hashes)',
            'runtime_evidence':'S6-theme-verification.json; single Theme run retained',
            'limits':['This proves Theme source/key membership and recorded canonical display boundaries only.',
                      'Remaining candidate call-path decisions are recorded in S6-remaining-source-audit.json; membership is not UI acceptance.',
                      'User language/audio-domain review remains outstanding.'],
            'evidence':[{'file':str(p.relative_to(ROOT)),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in paths]}
    (OUT/'S6-theme-source-closure.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(f"Theme source/key/dual-table closure PASS: 238 display keys, 0 Theme gaps; {gaps['unmatched_occurrences']} other candidates and human review remain.")


if __name__=='__main__': main()
