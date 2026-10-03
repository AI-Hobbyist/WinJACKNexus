"""Supplemental source/catalog/deployment proof; never runs a module or UI test."""
import csv
import hashlib
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Docs/i18n'


def main():
    checks = []

    def check(value, description):
        if not value:
            raise AssertionError(description)
        checks.append(description)

    def read(name):
        return json.loads((OUT / name).read_text(encoding='utf-8'))

    def sha(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    manifest = json.loads((ROOT / 'locate/manifest.json').read_text(encoding='utf-8'))
    required = {(domain, key) for domain, keys in manifest['requiredKeys'].items() for key in keys}
    check(manifest['catalogVersion'] == 11 and len(required) == 780, 'root v11 contains 780 required keys')
    with (OUT / 'S6-supplement-strings.tsv').open(encoding='utf-8-sig', newline='') as file:
        rows = list(csv.DictReader(file, delimiter='\t'))
    check(len(rows) == 9 and len({(r['domain'], r['key']) for r in rows}) == 9, 'nine unique supplemental display entries')
    artifacts = []
    tokens = lambda value: sorted(re.findall(r'\{[a-zA-Z][a-zA-Z0-9_]*\}|%[1-9][0-9]*', value))
    for language in manifest['languages']:
        path = ROOT / 'locate' / manifest['languages'][language]
        data = json.loads(path.read_text(encoding='utf-8'))
        old = json.loads(subprocess.check_output(['git', 'show', 'bd1a8f6:locate/' + path.name], cwd=ROOT))
        pairs = {(domain, key) for domain, values in data['domains'].items() for key in values}
        check(pairs == required and data['catalogVersion'] == 11, language + ' complete required key membership')
        added = pairs - {(domain, key) for domain, values in old['domains'].items() for key in values}
        check(len(added) == 6, language + ' exactly six added keys')
        check(all(data['domains'][d][k] == v for d, values in old['domains'].items() for k, v in values.items()), language + ' existing translations retained')
        for row in rows:
            value = data['domains'][row['domain']][row['key']]
            check(value == row[language] and tokens(value) == tokens(row['en-US']), language + ':' + row['key'] + ' term sheet and placeholders match')
        deployed = ROOT / 'build/release/bin/locate' / path.name
        check(path.read_bytes() == deployed.read_bytes(), language + ' deployed bytes match source')
        artifacts.extend([path, deployed])
    manifest_path = ROOT / 'locate/manifest.json'
    check(manifest_path.read_bytes() == (ROOT / 'build/release/bin/locate/manifest.json').read_bytes(), 'deployed manifest matches source')
    artifacts.append(manifest_path)
    source_report = read('S0-verification.json')
    check(source_report['result'] == 'PASS' and source_report['source_occurrences'] == 10420, 'fresh source inventory reconciled')
    gaps = read('S6-coverage-gaps.json')
    check(gaps['unmatched_occurrences'] == 0 and not gaps['rows'], 'all proposed display keys map to registered resources')
    audit = read('S6-remaining-source-audit.json')
    baseline = json.loads(subprocess.check_output(['git', 'show', 'bd1a8f6:Docs/i18n/S6-coverage-gaps.json'], cwd=ROOT))
    check({r['source_id'] for r in audit['rows']} == {r['source_id'] for r in baseline['rows']}, 'all 59 original remaining occurrences have individual decisions')
    check(all(r['reason'] and r['disposition'] in {'preserve', 'canonical_display_mapped', 'fix_applied'} for r in audit['rows']), 'each decision has its recorded call-path reason')
    for item in audit['evidence']:
        check(sha(ROOT / item['file']) == item['sha256'], 'reviewed source fingerprint: ' + item['file'])
        artifacts.append(ROOT / item['file'])
    review = (ROOT / 'i18n翻译核对.md').read_text(encoding='utf-8')
    translated, excluded = review.split('## 表二：已忽略翻译的部分', 1)
    check(all(translated.count('| ' + r['domain'] + ' | ' + r['key'] + ' |') == 1 for r in rows), 'supplemental translations occur once in table one')
    check(excluded.count('| S6 supplemental ') == 56, '56 preserved/canonical occurrences recorded in table two')
    role = (ROOT / 'src/meter/mod/src/MeterSettingsWidget.cpp').read_text(encoding='utf-8')
    check('roleSelector->currentData().toString()' in role and 'roleSelector->addItem(channel.speakerRole, channel.speakerRole)' in role and 'const QSignalBlocker blocker(roleSelector)' in role, 'role source separates original data and blocked display refresh')
    library = (ROOT / 'src/mixer/Library.cpp').read_text(encoding='utf-8')
    check('modules = effects_.unavailableModules()' in library and 'notice->setTextFormat(Qt::PlainText)' in library and 'warning == lastEffectWarning_' in library, 'warning source formats original IDs with plain text and canonical deduplication')
    build = OUT / 'S6-supplement-build.log'
    log = build.read_text(encoding='utf-8-sig')
    outputs = ['WinJACKNexus.exe', 'ui.dll', 'nodes/mixer/mod_digital_mixer.dll', 'mod_meterbrigde.dll', 'wjn_mixer_meter_i18n_tests.exe']
    for output in outputs:
        path = ROOT / 'build/release/bin' / output
        check(path.is_file() and path.stat().st_size > 0 and path.name in log, 'required Release output linked: ' + output)
        artifacts.append(path)
    check('MixerMeterI18nTests.cpp' in log, 'modified call-site checks compiled, not executed')
    artifacts.extend([build, OUT / 'S6-supplement-strings.tsv', OUT / 'S6-remaining-source-audit.json', OUT / 'S0-verification.json', ROOT / 'Develop/MixerMeterI18nTests.cpp'])
    report = {'stage': 'S6', 'date': '2026-10-04', 'result': 'SOURCE_CATALOG_DEPLOYMENT_AND_RELEASE_PASS_RUNTIME_UNVERIFIED',
              'catalog_version': 11, 'required_root_keys': 780, 'new_keys': 6, 'reused_keys': 3,
              'remaining_candidates_reviewed': 59, 'unmatched_occurrences': 0,
              'module_test_runs_added': 0, 'ui_shows_added': 0, 'build_exit_code': 0,
              'checks': checks, 'limits': ['Source checks do not prove runtime behavior, rendering, or translation quality.',
                  'Existing modules are not rerun. Supplemental role, legend, error and missing-FX notice behavior has no fresh UI acceptance.',
                  'Historical S4/S5/Theme runtime reports retain their original baselines; new DLL hashes are build artifacts only.',
                  'Four-language and audio-domain human review remains pending with the user.'],
              'evidence': [{'file': str(p.relative_to(ROOT)).replace('\\', '/'), 'sha256': sha(p)} for p in dict.fromkeys(artifacts)]}
    (OUT / 'S6-supplement-verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('S6 supplemental source/catalog/deployment/Release PASS; 0 module runs, 0 UI shows; runtime and human review remain unverified.')


if __name__ == '__main__':
    main()
