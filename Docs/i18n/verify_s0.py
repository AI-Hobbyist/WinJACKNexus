"""Offline verification of S0 evidence and inventory reconciliation."""
import csv
import hashlib
import json
import re
import sys
from collections import Counter
from pathlib import Path

sys.dont_write_bytecode = True
from scan_s0 import ROOT, OUT, collect
from classify_s0 import descriptor_fields

def read(name):
    with (OUT/name).open(encoding='utf-8-sig',newline='') as file:
        return list(csv.DictReader(file))

def signature(row):
    return tuple(str(row[key]) for key in ('id','file','line','literal','context','evidence_class'))

def main():
    checks = []
    def check(condition, name):
        if not condition: raise AssertionError(name)
        checks.append(name)
    manifest=json.loads((OUT/'S0-source-manifest.json').read_text(encoding='utf-8'))
    files,current=collect()
    source=read('S0-source-literals.csv')
    classified=read('S0-classified-literals.csv')
    keys=read('S0-keys.csv')
    excluded=read('S0-exclusions.csv')
    structured=read('S0-structured.csv')
    fields=read('S0-descriptor-fields.csv')
    resources=read('S0-resource-fields.csv')
    catalog=read('S0-key-catalog.csv')
    coverage=read('S0-file-coverage.csv')
    check(files==manifest['files'],'full source file set and SHA-256 match the current workspace')
    check(Counter(map(signature,current))==Counter(map(signature,source)),'source literal evidence matches a fresh extraction including occurrence multiplicity')
    check(Counter(map(signature,source))==Counter(map(signature,classified)),'all literal occurrences have exactly one classification')
    counts=Counter(r['disposition'] for r in classified)
    check(set(counts)<= {'translate','exclude','structured'},'no unresolved or unknown classifications')
    check(not read('S0-pending.csv'),'pending inventory is empty')
    check(counts['translate']==len(keys) and counts['exclude']==len(excluded) and counts['structured']==len(structured),'key/exclusion/structured inventories reconcile with the classified source')
    check(all(r['reason'] and r['preservation'] for r in excluded),'every fixed literal exclusion has a reason and preservation policy')
    check(all(r['key'] and r['domain'] and r['layout'] and r['source_window'] for r in keys),'every translatable occurrence records a key, domain, control/layout and surrounding source evidence')
    check(all(re.fullmatch(r'[A-Za-z][A-Za-z0-9_.-]*',r['key']) and not r['key'].startswith('s0.') for r in keys+catalog),'public key proposals are semantic identifiers, not source hashes')
    check(all(r['placeholders']==','.join(re.findall(r'%(?:L?\d+|n)|\{[A-Za-z_]\w*\}',r['source'])) for r in keys),'placeholder inventory agrees with every source template')
    schemas={}
    for row in keys:
        key=(row['domain'],row['key'])
        schema=tuple(row['placeholders'].split(',')) if row['placeholders'] else ()
        if key in schemas and schemas[key]!=schema:
            raise AssertionError('same-key placeholder mismatch: '+key[0]+'/'+key[1])
        schemas[key]=schema
    checks.append('same-key placeholder compatibility across all source occurrences')
    expected={(r['domain'],r['key']) for r in keys if r['usage']!='identity-guard'}
    expected.update((r['domain'],r['key']) for r in fields+resources if r['disposition']=='translate')
    actual=[(r['domain'],r['key']) for r in catalog]
    check(len(set(actual))==len(actual) and set(actual)==expected,'unique catalog exactly matches display keys and descriptor/resource fields')
    for row in structured:
        sub=[r for r in fields if r['source_id']==row['id']]
        actual_fields={(r['field'],r['source'],r['disposition']=='translate') for r in sub}
        check(actual_fields==set(descriptor_fields(json.loads(row['literal']))),'all descriptor string fields accounted: '+row['file'])
    resource_manifest=json.loads((OUT/'S0-resource-manifest.json').read_text(encoding='utf-8'))
    check(all(hashlib.sha256((ROOT/r['file']).read_bytes()).hexdigest()==r['sha256'] for r in resource_manifest),'QRC and external/bundled metadata resources match their recorded hashes')
    check(len(coverage)==len(files) and {r['file'] for r in coverage}=={r['file'] for r in files},'file coverage includes every scanned source file, including files without literals')
    check(sum(int(r['literal_occurrences']) for r in coverage)==len(source),'file occurrence totals match the source inventory')
    domains={r['domain'] for r in catalog}
    required={'host','patchbay','mixer','aux','meterbridge','adapter','fxhost','theme','ui','vst','vstscanner','example.node','example.nativefx'}
    required.update('nativefx.'+name for name in ('gate','graphic-eq','deesser','compressor','limiter','delay','parametric-eq','reverb'))
    check(required<=domains,'all required UI areas, eight effects and both developer examples have display keys')
    identity=read('S0-identity-uses.csv')
    check(all(any(c['id']==r['source_id'] and c['file']==r['file'] and c['line']==r['line'] for c in classified) for r in identity),'identity comparison evidence resolves to current source occurrences')
    delta_path=OUT/'S0-refresh-delta.json'
    if delta_path.exists():
        delta=json.loads(delta_path.read_text(encoding='utf-8'))
        current_pairs={(r['file'],r['literal']) for r in current}
        check(all((r['file'],r['literal']) in current_pairs for r in delta['added']),'refresh-added literals exist in the current source')
        check(all((r['file'],r['literal']) not in current_pairs for r in delta['removed']),'refresh-removed literals no longer exist in the recorded files')
        check(all((r['domain'],r['key']) in set(actual) for r in delta.get('catalog_added',[])),'refresh-added keys are in the active catalog')
        check(all((r['domain'],r['key']) not in set(actual) for r in delta.get('catalog_retired',[])),'retired keys are absent from the active catalog')
        current_evidence=Counter(map(signature,current))
        check(all(not (Counter(map(signature,item['after']))-current_evidence) for item in delta['usage_changed']),'changed literal usages match their current contexts and occurrence counts')
    report={'stage':'S0','date':'2026-10-03','result':'PASS',
        'source_files':len(files),'source_occurrences':len(source),'source_classifications':dict(counts),
        'display_key_proposals':len(catalog),'descriptor_fields':len(fields),'resource_fields':len(resources),
        'identity_comparison_occurrences':len(identity),'required_domains':sorted(required),
        'checks':list(dict.fromkeys(checks)),
        'limits':['S0 source inventory verification only; not a translation accuracy or UI rendering acceptance.',
            'No application/DLL/API/port changes; no Release build or JACK integration test required for this documentation stage.',
            'Dynamic display sources and indirect identity dependencies are reviewed in the companion Markdown documents.']}
    (OUT/'S0-verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(f"S0 PASS: {len(files)} source files, {len(source)} occurrences, {len(catalog)} display keys, no pending rows.")

if __name__=='__main__': main()
