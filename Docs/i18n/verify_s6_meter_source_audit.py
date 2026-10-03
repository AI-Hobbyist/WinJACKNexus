"""Reconcile explicit Meter source decisions; unresolved UI labels must remain open."""
import csv
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Docs/i18n'


def main():
    audit_path=OUT/'S6-meter-source-audit.json'
    audit=json.loads(audit_path.read_text(encoding='utf-8'))
    assert len(audit['rows'])==49 and audit['dispositions']=={'preserve':47,'fix_required':2}
    for evidence in audit['evidence']:
        assert hashlib.sha256((ROOT/evidence['file']).read_bytes()).hexdigest()==evidence['sha256']
    with (OUT/'S0-classified-literals.csv').open(encoding='utf-8-sig',newline='') as file:
        classified=list(csv.DictReader(file))
    for row in audit['rows']:
        actual=[r for r in classified if (r['file'],r['line'],r['literal'],r['context'])
                ==(row['file'],row['line'],row['source'],row['control_context'])]
        assert len(actual)==1
        assert actual[0]['disposition']==('exclude' if row['disposition']=='preserve' else 'translate')
    gaps=json.loads((OUT/'S6-coverage-gaps.json').read_text(encoding='utf-8'))
    meter=[r for r in gaps['rows'] if r['domain']=='meterbridge']
    assert len(meter)==2 and all(r['source']=='未分配' for r in meter)
    assert gaps['unmatched_occurrences']==59
    # This public static helper has a declaration and definition, no app caller.
    occurrences=[]
    for base in ('src','tools','Develop'):
        for path in (ROOT/base).rglob('*'):
            if path.suffix not in ('.cpp','.hpp','.h','.inc') or 'Tests' in path.name: continue
            if 'scenarioDisplayName(' in path.read_text(encoding='utf-8-sig'):
                occurrences.append(str(path.relative_to(ROOT)).replace('\\','/'))
    assert set(occurrences)=={'src/meter/ui/src/MockMeterDataProvider.cpp',
                              'src/meter/ui/include/WinJACKNexus/Meter/MockMeterDataProvider.hpp'}
    result={'stage':'S6','result':'METER_SOURCE_AUDIT_PASS_2_CONFIRMED_UI_GAPS_OPEN',
            'reviewed_occurrences':49,'preserved_occurrences':47,'confirmed_ui_gaps':2,
            'all_remaining_occurrences':59,'module_test_runs_added':0,'ui_shows_added':0,
            'source_audit_sha256':hashlib.sha256(audit_path.read_bytes()).hexdigest(),
            'limits':['No application changes, build or runtime execution in this source review.',
                      'The two unassigned labels still require translated display with canonical role item data.',
                      'Other candidates and user human review remain outstanding.']}
    (OUT/'S6-meter-source-verification.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('Meter source audit PASS: 47 explicit preservation decisions, 2 confirmed UI gaps retained; 59 total candidates remain.')


if __name__=='__main__': main()
