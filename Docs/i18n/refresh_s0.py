"""Capture changes against the recorded S0 baseline before regenerating it."""
import csv
import json
import sys
from collections import defaultdict

sys.dont_write_bytecode = True
from scan_s0 import ROOT, OUT, collect, main as scan

def main():
    old_manifest=json.loads((OUT/'S0-source-manifest.json').read_text(encoding='utf-8'))
    with (OUT/'S0-source-literals.csv').open(encoding='utf-8-sig',newline='') as f:
        old=list(csv.DictReader(f))
    files,new=collect()
    before={r['file']:r['sha256'] for r in old_manifest['files']}
    after={r['file']:r['sha256'] for r in files}
    changed=[f for f in sorted(before.keys() | after.keys()) if before.get(f)!=after.get(f)]
    def group(rows):
        result=defaultdict(list)
        for row in rows: result[(row['file'],row['literal'])].append(row)
        return result
    old_groups,new_groups=group(old),group(new)
    added,removed,relocated,usage_changed=[],[],[],[]
    for key in sorted(old_groups.keys() | new_groups.keys()):
        a,b=old_groups[key],new_groups[key]
        if not a: added.extend(b)
        elif not b: removed.extend(a)
        else:
            if len(a)!=len(b) or [r['context'] for r in a]!=[r['context'] for r in b]:
                usage_changed.append({'file':key[0],'literal':key[1],'before':a,'after':b})
            elif [str(r['line']) for r in a]!=[str(r['line']) for r in b]:
                relocated.append({'file':key[0],'literal':key[1],'before_lines':[r['line'] for r in a],
                    'after_lines':[r['line'] for r in b]})
    old_resources=json.loads((OUT/'S0-resource-manifest.json').read_text(encoding='utf-8'))
    import hashlib
    resource_changes=[r['file'] for r in old_resources
        if not (ROOT/r['file']).exists() or hashlib.sha256((ROOT/r['file']).read_bytes()).hexdigest()!=r['sha256']]
    delta={'date':'2026-10-04','changed_files':changed,'resource_changes':resource_changes,
        'before_occurrences':len(old),'after_occurrences':len(new),
        'added':added,'removed':removed,'relocated':relocated,'usage_changed':usage_changed}
    with (OUT/'S0-key-catalog.csv').open(encoding='utf-8-sig',newline='') as f:
        delta['catalog_before']=[{k:r[k] for k in ('domain','key','sources')} for r in csv.DictReader(f)]
    (OUT/'S0-refresh-delta.json').write_text(json.dumps(delta,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'changed_files':changed,'resource_changes':resource_changes,
        'added':len(added),'removed':len(removed),'relocated_groups':len(relocated),
        'usage_changed_groups':len(usage_changed),'before':len(old),'after':len(new)},ensure_ascii=False))
    for label,rows in (('ADDED',added),('REMOVED',removed)):
        for r in rows: print(f"{label} {r['file']}:{r['line']} {r['literal']!r} | {r['context']}")
    scan()

def finalize():
    path=OUT/'S0-refresh-delta.json'
    delta=json.loads(path.read_text(encoding='utf-8'))
    if 'catalog_before' not in delta:
        raise RuntimeError('This refresh is already finalized; do not overwrite its baseline.')
    before={(r['domain'],r['key']):r for r in delta.pop('catalog_before')}
    with (OUT/'S0-key-catalog.csv').open(encoding='utf-8-sig',newline='') as f:
        after={(r['domain'],r['key']):r for r in csv.DictReader(f)}
    delta['catalog_added']=[after[k] for k in sorted(after.keys()-before.keys())]
    delta['catalog_retired']=[before[k] for k in sorted(before.keys()-after.keys())]
    delta['catalog_changed_sources']=[{'before':before[k],'after':after[k]}
        for k in sorted(before.keys() & after.keys()) if before[k]['sources']!=after[k]['sources']]
    path.write_text(json.dumps(delta,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(f"Refresh finalized: {len(delta['catalog_added'])} added keys, {len(delta['catalog_retired'])} retired keys.")

if __name__=='__main__':
    if '--finalize' in sys.argv: finalize()
    else: main()
