"""Verify the checked-in S1 packages against the keywords used by their schemas.

This deliberately small evaluator is limited to these repository schemas;
it is not a general JSON Schema library. Runtime semantic checks live in C++.
"""
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent

def validate(value, schema, location):
    def check(condition, reason):
        if not condition:
            raise AssertionError(f'{location}: {reason}')
    kind = schema.get('type')
    if kind:
        check({'object': isinstance(value, dict), 'array': isinstance(value, list),
               'string': isinstance(value, str), 'integer': type(value) is int}[kind], kind)
    if 'const' in schema:
        check(type(value) is type(schema['const']) and value == schema['const'], 'const')
    if 'enum' in schema:
        check(value in schema['enum'], 'enum')
    if 'oneOf' in schema:
        accepted = 0
        for branch in schema['oneOf']:
            try:
                validate(value, branch, location)
                accepted += 1
            except AssertionError:
                pass
        check(accepted == 1, 'oneOf')
    if isinstance(value, str):
        check(len(value) >= schema.get('minLength', 0), 'minLength')
        check(len(value) <= schema.get('maxLength', len(value)), 'maxLength')
        if 'pattern' in schema:
            check(re.fullmatch(schema['pattern'], value) is not None, 'pattern')
    elif type(value) is int:
        check(value >= schema.get('minimum', value), 'minimum')
        check(value <= schema.get('maximum', value), 'maximum')
    elif isinstance(value, list):
        check(len(value) >= schema.get('minItems', 0), 'minItems')
        if schema.get('uniqueItems'):
            check(len({json.dumps(item, sort_keys=True) for item in value}) == len(value), 'uniqueItems')
        for index, item in enumerate(value):
            validate(item, schema.get('items', {}), f'{location}[{index}]')
    elif isinstance(value, dict):
        check(len(value) >= schema.get('minProperties', 0), 'minProperties')
        check(set(schema.get('required', [])) <= value.keys(), 'required')
        properties = schema.get('properties', {})
        for key, item in value.items():
            if 'propertyNames' in schema:
                validate(key, schema['propertyNames'], location + '/' + key)
            if key in properties:
                validate(item, properties[key], location + '/' + key)
            elif schema.get('additionalProperties') is False:
                check(False, 'additionalProperties: ' + key)
            elif isinstance(schema.get('additionalProperties'), dict):
                validate(item, schema['additionalProperties'], location + '/' + key)

def load(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise AssertionError(f'{path}: duplicate JSON key {key}')
            result[key] = value
        return result
    return json.loads(path.read_text(encoding='utf-8'), object_pairs_hook=unique)

def main():
    schemas = {name: load(OUT / 'schema' / f'{name}.schema.json')
               for name in ('manifest', 'descriptor', 'catalog')}
    packages = []
    for directory in ('locate', 'Develop/Node/locate', 'Develop/NativeFx/locate'):
        root = ROOT / directory
        manifest = load(root / 'manifest.json')
        validate(manifest, schemas['manifest'], directory + '/manifest.json')
        english = None
        for language, filename in manifest['languages'].items():
            catalog = load(root / filename)
            validate(catalog, schemas['catalog'], directory + '/' + filename)
            assert catalog['moduleId'] == manifest['moduleId']
            assert catalog['catalogVersion'] == manifest['catalogVersion']
            assert catalog['language'] == language
            if language == 'en-US':
                assert set(catalog['domains']) == set(manifest['domains'])
                for domain, keys in manifest['requiredKeys'].items():
                    assert set(catalog['domains'][domain]) == set(keys)
                english = catalog
        assert english is not None
        packages.append({'root': directory, 'moduleId': manifest['moduleId'],
                         'languages': list(manifest['languages']),
                         'english_keys': sum(map(len, english['domains'].values()))})
    test_log = (OUT / 'S1-test.log').read_text(encoding='utf-8-sig')
    assert '100% tests passed' in test_log
    count = re.search(r'S1 i18n PASS: (\d+) offline checks', test_log)
    assert count
    files = [ROOT / 'src/ui/src/I18nManager.cpp',
             ROOT / 'src/ui/include/WinJACKNexus/UI/I18nManager.hpp',
             ROOT / 'src/common/include/WinJACKNexus/Common/I18nApi.h',
             ROOT / 'Develop/I18nTests.cpp', ROOT / 'Develop/I18nCAbi.c',
             ROOT / 'Develop/I18nExample.hpp', ROOT / 'Develop/CMakeLists.txt',
             ROOT / 'Develop/Node/ExampleNode.cpp', ROOT / 'Develop/NativeFx/ExampleNativeFx.cpp',
             ROOT / 'src/ui/CMakeLists.txt', ROOT / 'src/ui/resources/ui.qrc',
             ROOT / 'src/native_fx/CMakeLists.txt',
             ROOT / 'src/node/CMakeLists.txt', ROOT / 'src/node/include/WinJACKNexus/Node/NodeApi.h',
             ROOT / 'src/native_fx/include/WinJACKNexus/NativeFx/NativeFxApi.h']
    for directory in ('locate', 'Develop/Node/locate', 'Develop/NativeFx/locate', 'Docs/i18n/schema'):
        files.extend((ROOT / directory).glob('*.json'))
    artifacts = ['build/release/bin/ui.dll', 'build/release/bin/node.dll',
                 'build/release/bin/examples/node/wjn_example_node.dll',
                 'build/release/bin/examples/native_fx/wjn_example_native_fx.dll',
                 'build/release/bin/wjn_i18n_tests.exe']
    assert 'ui.dll' in (OUT / 'S1-build.log').read_text(encoding='utf-8-sig')
    report = {'stage': 'S1', 'date': '2026-10-03', 'result': 'PASS',
              'configuration': 'Release', 'test': 'i18n_contract_tests',
              'test_assertions_including_fixture_checks': int(count[1]), 'packages': packages,
              'source_hashes': {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in sorted(files)},
              'artifact_hashes': {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in artifacts},
              'limits': ['S1 core and example verification; production window migration remains S2-S6.',
                         'Example translations await human review; English fallback is not a completed translation.',
                         'No JACK server or running application was used.']}
    (OUT / 'S1-verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(f"S1 assets PASS: {len(packages)} packages; schema/English key coverage; Release DLL/UI test passed.")

if __name__ == '__main__':
    main()
