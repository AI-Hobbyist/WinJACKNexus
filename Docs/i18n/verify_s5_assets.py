"""Reconcile S5 catalogs, call-site inventory, deployment and Windows test evidence.

This verifies evidence consistency; image inspection and human language review
remain independent gates.
"""
import csv
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Docs/i18n'
LANGUAGES = ('en-US', 'zh-CN', 'ja-JP', 'ko-KR')
EFFECTS = ('gate', 'graphic_eq', 'deesser', 'compressor', 'limiter', 'delay', 'parametric_eq', 'reverb')

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    runtime = ROOT / 'build/release/bin'
    records = json.loads((OUT/'S5-strings.json').read_text(encoding='utf-8'))
    assert len(records) == len({(r['effect'],r['key']) for r in records}) == 275
    audit = (ROOT/'i18n翻译核对.md').read_text(encoding='utf-8-sig')
    placeholders = lambda value: set(re.findall(r'%(?:L?\d+|n)|\{[A-Za-z_]\w*\}', value))
    identities, evidence = set(), []
    for effect in EFFECTS:
        source = ROOT/'src/native_fx/effects'/effect/'locate'
        destination = runtime/'nodes/native_fx'/effect/'locate'
        manifest = json.loads((source/'manifest.json').read_text(encoding='utf-8'))
        domain = 'nativefx.' + manifest['moduleId']
        selected = [r for r in records if r['effect']==effect]
        keys = {r['key'] for r in selected}
        assert keys == set(manifest['requiredKeys'][domain])
        identities.update((domain,key) for key in keys)
        assert (source/'manifest.json').read_bytes() == (destination/'manifest.json').read_bytes()
        dll = runtime/'nodes/native_fx'/effect/('mod_'+effect+'.dll')
        assert dll.is_file()
        evidence.append({'file':str(dll.relative_to(ROOT)), 'sha256':digest(dll)})
        for language in LANGUAGES:
            path = source/(language+'.json')
            catalog = json.loads(path.read_text(encoding='utf-8'))
            assert catalog['moduleId'] == manifest['moduleId'] and catalog['language']==language
            assert catalog['catalogVersion']==manifest['catalogVersion']
            assert set(catalog['domains'])=={domain} and set(catalog['domains'][domain])==keys
            assert path.read_bytes()==(destination/path.name).read_bytes()
            for row in selected:
                text = catalog['domains'][domain][row['key']]
                assert text.strip() and text==row[language]
                assert placeholders(text)==placeholders(row['en-US'])
                lines = [line for line in audit.splitlines() if line.startswith(f"| {domain} | {row['key']} |")]
                assert len(lines)==1 and text.replace('|','&#124;').replace('\n','<br>') in lines[0]
            evidence.append({'file':str(path.relative_to(ROOT)), 'sha256':digest(path)})
    shared = list(csv.DictReader((OUT/'S5-shared-strings.tsv').open(encoding='utf-8'),delimiter='\t'))
    assert len(shared)==5
    host_manifest = json.loads((ROOT/'locate/manifest.json').read_text(encoding='utf-8'))
    assert host_manifest['catalogVersion']==5 and sum(map(len,host_manifest['requiredKeys'].values()))==533
    for language in LANGUAGES:
        path = ROOT/'locate'/(language+'.json')
        catalog = json.loads(path.read_text(encoding='utf-8'))
        assert path.read_bytes()==(runtime/'locate'/path.name).read_bytes()
        for row in shared:
            assert catalog['domains'][row['domain']][row['key']]==row[language]
            assert row['key'] in host_manifest['requiredKeys'][row['domain']]
            lines = [line for line in audit.splitlines() if line.startswith(f"| {row['domain']} | {row['key']} |")]
            assert len(lines)==1 and row[language] in lines[0]
    with (OUT/'S0-keys.csv').open(encoding='utf-8-sig',newline='') as file:
        scope=[r for r in csv.DictReader(file) if r['file'].startswith('src/native_fx/')]
    assert scope and all((r['domain'],r['key']) in identities for r in scope), [(r['file'],r['line'],r['key']) for r in scope if (r['domain'],r['key']) not in identities]
    with (OUT/'S0-pending.csv').open(encoding='utf-8-sig',newline='') as file:
        assert not list(csv.DictReader(file))
    runs=[]
    for scale in ('1','1.5','2'):
        for cold in ('',)+LANGUAGES:
            suffix = '-cold-'+cold if cold else ''
            path=OUT/f'S5-tests-scale-{scale}{suffix}.log'
            log=path.read_text(encoding='utf-8-sig')
            match=re.search(r'S5 module Windows checks PASS: (\d+); platform=windows font=(.+?) scale='+re.escape(scale),log)
            assert match and int(match[1])>0, path
            rendered=OUT/'S5-rendered'/('scale-'+scale)/('cold-'+cold if cold else 'hot')
            images=list(rendered.glob('*.png'))
            assert images, rendered
            locales=(cold,) if cold else LANGUAGES
            for effect in EFFECTS:
                for locale in locales:
                    for kind in ('mixer','node','preset-menu','draft'):
                        assert list(rendered.glob(effect+'-'+kind+'-'+locale+'*.png')), (effect,kind,locale,scale)
                assert list(rendered.glob(effect+'-partial-fallback*.png')), effect
            runs.append({'scale':scale,'cold':cold,'checks':int(match[1]),'font':match[2],
                         'captures':len(images),'log':str(path.relative_to(ROOT)), 'sha256':digest(path)})
    source_paths=[ROOT/'src/native_fx/src/I18n.cpp', ROOT/'src/native_fx/src/NativeFxApi.cpp', ROOT/'src/ui/src/Controls.cpp',
                  ROOT/'src/ui/src/FxSettingsWindow.cpp',ROOT/'Develop/NativeFx/NativeFxI18nTests.cpp']
    source_paths.extend(ROOT/'src/native_fx/effects'/effect/'src/Ui.cpp' for effect in EFFECTS)
    evidence.extend({'file':str(path.relative_to(ROOT)), 'sha256':digest(path)} for path in source_paths)
    report={'stage':'S5','result':'PASS','scope':'catalog/deployment/inventory/Windows test evidence consistency',
            'module_keys':len(records),'shared_keys':len(shared),'source_display_occurrences':len(scope),
            'runs':runs,'evidence':evidence,'human_review':'pending S6; automated checks do not approve translations'}
    (OUT/'S5-verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(f'S5 assets PASS: {len(records)} module keys, {len(shared)} shared keys, {len(scope)} source displays, {sum(r["captures"] for r in runs)} native Windows captures.')

if __name__=='__main__':
    main()
