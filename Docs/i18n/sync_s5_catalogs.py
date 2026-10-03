"""Generate Native Fx draft catalogs from reviewed explicit records, not approval."""
import csv
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Docs/i18n'
LANGUAGES = ('en-US', 'zh-CN', 'ja-JP', 'ko-KR')

def main():
    with (OUT / 'S5-strings.tsv').open(encoding='utf-8', newline='') as f:
        rows = list(csv.DictReader(f, delimiter='\t'))
    assert len(rows) == len({(r['effect'], r['key']) for r in rows})
    placeholders = lambda s: set(re.findall(r'%(?:L?\d+|n)|\{[A-Za-z_]\w*\}', s))
    for r in rows:
        assert all(r[l].strip() for l in LANGUAGES), r
        assert all(placeholders(r[l]) == placeholders(r['en-US']) for l in LANGUAGES), r
    for effect in ('gate', 'graphic_eq', 'deesser', 'compressor', 'limiter', 'delay', 'parametric_eq', 'reverb'):
        base = ROOT / 'src/native_fx/effects' / effect
        module_id = re.search(r'activeMeta\s*\{\s*"([^"]+)"', (base / 'Meta.hpp').read_text(encoding='utf-8'))[1]
        domain = 'nativefx.' + module_id
        selected = [r for r in rows if r['effect'] == effect]
        assert {'module.name', 'module.description'} <= {r['key'] for r in selected}
        locate = base / 'locate'
        locate.mkdir(exist_ok=True)
        manifest = {'formatVersion': 1, 'moduleId': module_id, 'domains': [domain],
                    'defaultLanguage': 'en-US', 'catalogVersion': 1,
                    'languages': {l: l + '.json' for l in LANGUAGES},
                    'requiredKeys': {domain: [r['key'] for r in selected]}}
        def save(name, value):
            (locate / name).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        save('manifest.json', manifest)
        for language in LANGUAGES:
            save(language + '.json', {'formatVersion': 1, 'catalogVersion': 1, 'moduleId': module_id,
                 'language': language, 'domains': {domain: {r['key']: r[language] for r in selected}}})
    (OUT / 'S5-strings.json').write_text(json.dumps(rows, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    with (OUT / 'S5-shared-strings.tsv').open(encoding='utf-8', newline='') as f:
        shared = list(csv.DictReader(f, delimiter='\t'))
    assert len(shared) == len({(r['domain'], r['key']) for r in shared})
    for r in shared:
        assert all(r[l].strip() for l in LANGUAGES), r
        assert all(placeholders(r[l]) == placeholders(r['en-US']) for l in LANGUAGES), r
    host_root = ROOT / 'locate'
    manifest = json.loads((host_root / 'manifest.json').read_text(encoding='utf-8'))
    manifest['catalogVersion'] = max(5, manifest['catalogVersion'])
    for r in shared:
        required = manifest['requiredKeys'].setdefault(r['domain'], [])
        if r['key'] not in required:
            required.append(r['key'])
        if r['domain'] not in manifest['domains']:
            manifest['domains'].append(r['domain'])
    (host_root / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    for language in LANGUAGES:
        path = host_root / (language + '.json')
        catalog = json.loads(path.read_text(encoding='utf-8'))
        catalog['catalogVersion'] = manifest['catalogVersion']
        for r in shared:
            catalog['domains'].setdefault(r['domain'], {})[r['key']] = r[language]
        path.write_text(json.dumps(catalog, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    sync_review(rows, shared)
    print(f'S5 draft catalogs synchronized: {len(rows)} keys; implementation/Release/UI review pending.')


def sync_review(rows, shared):
    """Record draft translations at actual source calls; never grant review approval."""
    records = []
    for row in rows:
        base = ROOT / 'src/native_fx/effects' / row['effect']
        module_id = re.search(r'activeMeta\s*\{\s*"([^"]+)"', (base / 'Meta.hpp').read_text(encoding='utf-8'))[1]
        locations = []
        for source in (base / 'src/Ui.cpp', base / 'include/Logic.inc'):
            for number, line in enumerate(source.read_text(encoding='utf-8').splitlines(), 1):
                if '"' + row['key'] + '"' in line:
                    locations.append(f'{source.relative_to(ROOT).as_posix()}:{number}')
        if row['key'] == 'module.name':
            locations.append('src/native_fx/src/I18n.cpp (displayNameKey)')
        if row['key'].startswith('preset.'):
            for number, line in enumerate((ROOT / 'src/native_fx/src/NativeFxApi.cpp').read_text(encoding='utf-8').splitlines(), 1):
                if '"' + row['key'] + '"' in line:
                    locations.append(f'src/native_fx/src/NativeFxApi.cpp:{number} (module branch)')
        assert locations, f'No S5 call site: {row["effect"]}/{row["key"]}'
        records.append(dict(row, domain='nativefx.' + module_id, location='；'.join(locations)))
    source = ROOT / 'src/ui/src/Controls.cpp'
    for row in shared:
        locations = [f'src/ui/src/Controls.cpp:{n}' for n, line in enumerate(source.read_text(encoding='utf-8').splitlines(), 1)
                     if '"' + row['key'] + '"' in line]
        assert locations, row
        records.append(dict(row, location='；'.join(locations)))
    path = ROOT / 'i18n翻译核对.md'
    results, exclusions = path.read_text(encoding='utf-8-sig').split('## 表二：已忽略翻译的部分', 1)
    results = '\n'.join(line for line in results.splitlines()
                        if not line.startswith('| nativefx.') and not line.startswith('| ui | clientName.')) + '\n'
    def cell(value):
        return value.replace('|', '&#124;').replace('\n', '<br>')
    additions = ['| ' + ' | '.join([r['domain'], r['key'], cell(r['zh-CN']+'；'+r['location'])]
                 + [cell(r[l]) for l in ('zh-CN', 'en-US', 'ja-JP', 'ko-KR')]
                 + [cell(r['review'])+'；S5 实施中，未验收', '2026-10-03']) + ' |' for r in records]
    position = results.index('\n| host |')
    results = results[:position] + '\n' + '\n'.join(additions) + results[position:]
    exclusions='\n'.join(line for line in exclusions.splitlines() if not line.startswith('| S5 '))+'\n'
    additions = [
        '| S5 SDK 身份 | Meta.id、原始 Meta.name、NodeInfo 名称/分类/版本/说明 | src/native_fx/effects/*/Meta.hpp、include/Logic.inc | DLL 发现及既有 SDK 元数据身份 | 原始值保留；module.name/module.description 单独翻译可见副本 | S0-exclusions.csv 逐出现位置记录；实窗验证待执行 | 2026-10-03 |',
        '| S5 用户与机器数据 | 用户客户端名称、文件名/路径、JSON 字段、参数字节、预设索引、声道索引 | 八模块节点/FX 实例、Graphic EQ 文件对话框、ClientNameWidget | 用户内容和保存/路由协议身份 | 原样，周边标签与操作说明独立翻译 | 参数快照、待提交输入及对话框的实窗验证待执行 | 2026-10-03 |',
        '| S5 语言与主题身份 | 域/key、optional ABI 导出、locate/schema、主题 token、颜色与绘制数值 | I18n.cpp、NativeFxApi.cpp、公共 Controls 及各 Ui.cpp | 注册协议、主题和绘制参数，非操作说明 | 原样；实际 English 显示基线另列 | S0-exclusions.csv 包含逐调用点原因；语言包 manifest 单独对账 | 2026-10-03 |',
    ]
    technical={}
    with (OUT/'S0-exclusions.csv').open(encoding='utf-8-sig',newline='') as file:
        for row in csv.DictReader(file):
            if not row['file'].startswith('src/native_fx/effects/') or not row['file'].endswith('/src/Ui.cpp'):
                continue
            if not re.search(r'QStringLiteral|(?:label|textAt|text|drawText)\(|(?:channels|channelNames)\[',row['context']):
                continue
            text=row['source']
            if not text or text.startswith(('#','--','wjn.')) or re.search(r'[{};]|\.(?:hpp|h)$',text):
                continue
            technical.setdefault((row['domain'],text),[]).append(row)
    for (domain,text), items in sorted(technical.items()):
        locations='；'.join(sorted({r['file']+':'+r['line'] for r in items}))
        reasons='；'.join(sorted({r['reason'] for r in items}))
        additions.append('| '+' | '.join(['S5 '+domain,cell(text),cell(locations),cell(reasons),'原样；周边参数/操作说明独立翻译','源码分类已记录；实窗待验证','2026-10-03'])+' |')
    for row in rows:
        if not row['key'].startswith('hint.'):
            continue
        additions.append('| '+' | '.join(['S5 '+row['effect'],cell(row['en-US']),row['key'],
                         '已翻译主标签旁的音频参数 English 对照术语，保留既有参考窗口形式',
                         '括号 English 对照术语原样；主标签已有四语言译文','待 S6 领域审校确认保留形式','2026-10-03'])+' |')
    exclusions+='\n'+'\n'.join(additions)+'\n'
    path.write_text(results+'## 表二：已忽略翻译的部分'+exclusions, encoding='utf-8')

if __name__ == '__main__':
    main()
