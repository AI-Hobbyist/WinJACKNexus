"""Generate the reviewed S0 inventories from source evidence and explicit decisions.

Rules classify obvious non-display constants; human overrides/key names live in
S0-decisions.json. Unresolved rows are retained and prevent completion.
"""
import csv
import json
import re
import sys
import hashlib
import zipfile
from collections import Counter
from pathlib import Path
sys.dont_write_bytecode = True
from scan_s0 import OUT, ROOT

def source_use(r):
    text, ctx = r['literal'], r['context']
    value = re.escape('"'+text+'"')
    guard = re.search(r'(?:==|!=)\s*(?:QStringLiteral\()?'+value,ctx) or re.search(value+r'\)?\s*(?:==|!=)',ctx)
    guard = guard or re.search(r'\.(?:compare|startsWith|endsWith|indexOf|findText)\s*\(\s*(?:QStringLiteral\()?'+value,ctx)
    return 'identity-guard' if guard else 'display-source'

def descriptor_fields(value, path=''):
    if isinstance(value,dict):
        for key,item in value.items():
            child = path+'.'+key if path else key
            if isinstance(item,str):
                yield child,item,key in ('displayName','description')
            else: yield from descriptor_fields(item,child)
    elif isinstance(value,list):
        for index,item in enumerate(value):
            identity = item.get('id',item.get('name',str(index))) if isinstance(item,dict) else str(index)
            yield from descriptor_fields(item,path+'.'+str(identity).strip('-'))

def write_csv(name, rows, fields=None):
    with (OUT/name).open('w', encoding='utf-8-sig', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=fields or list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

def read_decisions():
    path = OUT/'S0-decisions.json'
    decisions = json.loads(path.read_text(encoding='utf-8')) if path.exists() else {}
    semantic = OUT/'S0-semantic-map.tsv'
    if semantic.exists():
        for row in csv.DictReader(semantic.open(encoding='utf-8'),delimiter='\t'):
            decisions.setdefault('text:'+row['source'], {'kind':'translate','key':row['key'],
                'reason':'按源码使用场景指定语义 key；技术标识/单位/占位参数保持原样'})
    return decisions

def slug(text):
    words = re.findall(r'[a-zA-Z]+\d*',text)
    return '.'.join(w.lower() for w in words)[:160].strip('.')

def host_call(r):
    patterns = (r'(?:hostText|hostLabel|hostButton|hostToggle|hostGroup)\(\s*"([^"]+)"\s*,\s*"((?:\\.|[^"\\])*)"',
                r'(?:bindHostText|chooseDirectory)\([^,\n]+,\s*"(?:text|title|windowTitle|toolTip|accessibleName|placeholderText)",\s*"([^"]+)"\s*,\s*"((?:\\.|[^"\\])*)"',
                r'chooseDirectory\([^,\n]+,\s*"([^"]+)"\s*,\s*"((?:\\.|[^"\\])*)"')
    for pattern in patterns:
        for key, english in re.findall(pattern, r['context']):
            if r['literal'] == key: return 'exclude', key
            if r['literal'] == english: return 'translate', key
    return None

S3_RECORDS = json.loads((OUT/'S3-strings.json').read_text(encoding='utf-8'))
S3_FILES = {'src/adapter/src/Library.cpp', 'src/patchbay/mod/src/Library.cpp',
            'src/patchbay/mod/src/PatchbayCanvas.cpp',
            'src/patchbay/mod/include/mod_patchbay/LocalizedDialogs.hpp'}

def s3_call(r):
    if r['file'] not in S3_FILES:
        return None
    legacy_categories = {'根目录','Audio Adapter','Windows Native Driver','Sub Graph',
                         'Sub Graph Interface','Meter','Digital Mixer','Patchbay',
                         'jackclient','VST Plugins','Instruments','Effects'}
    if r['literal'] in legacy_categories and not re.search(
            r'patchText|bindPatchText|patchButton|patchMenu|patchAction|canvasText|canvasAction|PatchbayDialogs', r['context']):
        return None
    for row in S3_RECORDS:
        if r['literal'] == row['key']:
            return 'exclude', row['domain'], row['key']
        english = row['en-US'].replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n')
        if r['literal'] == english:
            return 'translate', row['domain'], row['key']
    return None

S4_RECORDS = json.loads((OUT/'S4-strings.json').read_text(encoding='utf-8'))
S4_SOURCES = {
    'aux': ('src/ui/src/AuxSettingsWindow.cpp',),
    'mixer': ('src/ui/src/MixerWindow.cpp', 'src/ui/src/MixerOverview.hpp', 'src/ui/src/LocalizedMixerErrors.hpp'),
    'fxhost': ('src/ui/src/FxSettingsWindow.cpp', 'src/ui/src/FxLevelMeterWidget.cpp'),
    'ui': ('src/ui/src/HistoryChartWidget.cpp', 'src/ui/src/MetricStatisticsWidget.cpp'),
    'meterbridge': ('src/meter/mod/', 'src/meter/probe/Library.cpp'),
}

def s4_call(r):
    if r['file']=='src/ui/src/MixerWindow.cpp' and r['literal'] in ('Inputs','Sub Mix/Monitor/Master') and 'group==0?' in r['context']:
        return 'translate', 'mixer', 'section.inputs' if r['literal']=='Inputs' else 'section.subMonitorMaster'
    domains = [domain for domain, paths in S4_SOURCES.items() if r['file'].startswith(paths)]
    for row in S4_RECORDS:
        if row['domain'] not in domains:
            continue
        if r['literal'] == row['key']:
            return 'exclude', row['domain'], row['key']
        spellings = {json.dumps(row['en-US'], ensure_ascii=ascii)[1:-1] for ascii in (True, False)}
        if r['literal'] in spellings and ('"'+row['key']+'"') in r['context']:
            return 'translate', row['domain'], row['key']
    return None

S5_RECORDS = json.loads((OUT/'S5-strings.json').read_text(encoding='utf-8'))
S5_IDS = ('gate','graphic-eq','deesser','compressor','limiter','delay','parametric-eq','reverb')

def s5_call(r):
    file, text, ctx = r['file'], r['literal'], r['context']
    records, domain = [], ''
    if file=='Develop/NativeFx/ExampleNativeFx.cpp' and not (
            'return {' in ctx or 'WjnFxMeta meta' in ctx or 'wjn_get_node_name' in ctx
            or text=='Fixed eight-channel gain example'):
        # Constructor English and its synchronous binding share one visible key.
        example_keys={'Example Gain':'name','Gain':'gain','Channel':'channel','All':'all'}
        if text in example_keys:
            return 'translate','example.nativefx',example_keys[text]
    if file.startswith('src/native_fx/effects/') and file.endswith(('Ui.cpp','Logic.inc')):
        effect = file.split('/')[3]
        records = [row for row in S5_RECORDS if row['effect']==effect]
        domain = 'nativefx.'+effect.replace('_','-')
    elif file=='src/native_fx/src/NativeFxApi.cpp':
        lines = (ROOT/file).read_text(encoding='utf-8').splitlines()
        branch = next((re.match(r'#(?:if|elif) WJN_FX_MODULE_INDEX == (\d+)\s*$',line)
                       for line in reversed(lines[:int(r['line'])])
                       if re.match(r'#(?:if|elif) WJN_FX_MODULE_INDEX == (\d+)\s*$',line)),None)
        if branch and ('keys[]' in ctx or 'english[]' in ctx):
            domain='nativefx.'+S5_IDS[int(branch[1])]
            records=[row for row in S5_RECORDS if row['effect'].replace('_','-')==S5_IDS[int(branch[1])]]
    elif file=='src/ui/src/Controls.cpp':
        records=list(csv.DictReader((OUT/'S5-shared-strings.tsv').open(encoding='utf-8'),delimiter='\t'))
        domain='ui'
    for row in records:
        if text==row['key']:
            return 'exclude',domain,row['key']
        spellings={json.dumps(row['en-US'],ensure_ascii=ascii)[1:-1] for ascii in (True,False)}
        if text in spellings and ('"'+row['key']+'"' in ctx or 'english[]' in ctx):
            return 'translate',domain,row['key']
    return None

def s6_call(r):
    if r['file'] in ('src/ui/src/ThemeManagementDialog.cpp', 'src/ui/src/LocalizedThemeDialogs.hpp'):
        # Helpers accept different first arguments, but the reviewed literal
        # English/key pair remains on the same line as the extracted occurrence.
        rows = list(csv.DictReader((OUT/'S6-theme-ui-strings.tsv').open(encoding='utf-8'), delimiter='\t'))
        for row in rows:
            if '"'+row['key']+'"' not in r['context']:
                continue
            if r['literal'] == row['key']:
                return 'exclude', 'theme', row['key']
            if r['literal'] == json.dumps(row['en-US'],ensure_ascii=False)[1:-1]:
                return 'translate', 'theme', row['key']
    if r['file'] == 'src/ui/src/LocalizedThemeMessages.hpp':
        match = re.match(r'\{\s*"([^"]+)"\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*\}', r['context'])
        if match:
            key, original, english = match.groups()
            if r['literal'] == english:
                return 'translate', 'theme', key
            if r['literal'] in (key, original):
                return 'exclude', 'theme', key
    if r['file'] == 'src/ui/src/Controls.cpp':
        for domain, key, english in re.findall(
                r'localizedText\(\s*"(ui)"\s*,\s*"([^"\n]+)"\s*,\s*"((?:\\.|[^"\\])*)"', r['context']):
            if r['literal'] in (domain, key):
                return 'exclude', domain, key
            if r['literal'] == english:
                return 'translate', domain, key
    if r['file'] == 'tools/vst_scanner/src/ProbeMain.cpp':
        # Transport English and structured key/args represent the same display
        # template resolved by the Scanner.cpp whitelist, not error identities.
        for english, key in re.findall(
                r'error\s*=\s*\{\s*"([^"\n]+)"[^\n]*?,\s*"(probe\.[A-Za-z0-9]+)"', r['context']):
            if r['literal'] == key:
                return 'exclude', 'vstscanner', key
            if r['literal'] == english:
                return 'translate', 'vstscanner', key
    calls = {
        'src/vst/src/Library.cpp': ('vst', 'vstText'),
        'tools/vst_scanner/src/MainWindow.cpp': ('vstscanner', 'scannerText'),
        'tools/vst_scanner/src/Scanner.cpp': ('vstscanner', 'scannerText'),
    }
    if r['file'] not in calls:
        return None
    domain, function = calls[r['file']]
    pattern = re.escape(function) + r'\(\s*"([^"\n]+)"\s*,\s*"((?:\\.|[^"\\])*)"'
    for key, english in re.findall(pattern, r['context']):
        if r['literal'] == key:
            return 'exclude', domain, key
        if r['literal'] == english:
            return 'translate', domain, key
    return None

def classify(r, decisions):
    text, ctx, file = r['literal'], r['context'], r['file']
    if file == 'src/ui/src/LocalizedThemeMessages.hpp':
        mapped = s6_call(r)
        if mapped and mapped[0] == 'exclude':
            return 'exclude','','Theme 显示适配器的稳定 key/canonical 匹配模板；实际 English 与四语言显示副本另列，不将原始 %1 与显示 detail 参数混为模板'
    if file == 'src/ui/src/ThemeManagementDialog.cpp' and 'ThemeMessages::canonical(' in ctx:
        rows = csv.DictReader((OUT/'S6-theme-ui-strings.tsv').open(encoding='utf-8'), delimiter='\t')
        if any(row['key'].startswith('error.') and text == row['en-US'] for row in rows):
            return 'exclude','','Theme authoring API 的 canonical English 错误；实际状态控件保存原文，通过 ThemeMessages::display 热刷新，完整显示模板已另列，禁止翻译 API payload'
    if file == 'src/ui/src/ThemeManagementDialog.cpp' and text == 'Built-in default appearance' and 'identity.defaultDescription' not in ctx:
        return 'exclude','','可编辑 description 的原始 metadata 缺省值；导出与用户内容保持，内置默认主题的只读显示由 theme/identity.defaultDescription 独立翻译'
    if file in ('src/ui/src/ThemeCore.cpp','src/ui/src/ThemePackage.cpp','src/ui/src/ThemeNamespaceRegistry.cpp','src/ui/src/ThemeCatalog.cpp'):
        for name in ('core','package-namespace','catalog'):
            rows = csv.DictReader((OUT/f'S6-theme-{name}-strings.tsv').open(encoding='utf-8'),delimiter='\t')
            if any(row['source_file'] == file and text == json.dumps(row['canonical'].replace('%1',''),ensure_ascii=False)[1:-1] for row in rows):
                return 'exclude','','Theme 后台 canonical 诊断/错误来源；GUI 通过 LocalizedThemeMessages 对已知完整错误翻译副本，模型、校验、业务比较、日志及参数原样；实际带 detail 的 English 模板另列'
    theme_non_display = {
        'src/ui/src/ThemeCore.cpp': {'User overrides','Per-theme user preferences'},
        'src/ui/src/ThemeCatalog.cpp': {': 默认主题由必需 DLL 提供',': 重复主题 ID ','（先发现 ',' (Dev)','无法恢复主题覆盖 ','主题已切换，但无法保存选择','无法恢复上次主题 '},
        'src/ui/src/ThemeBackground.cpp': {'Image dimensions are invalid or exceed 64 million pixels','Scaled image exceeds 64 million pixels',': missing image resource'},
        'src/ui/src/ThemeManager.cpp': {'Theme font resource is missing; using system font','Theme font registration failed; using system font','Theme font contains multiple families; using the first'},
    }
    if text in theme_non_display.get(file,set()):
        return 'exclude','','逐调用链核对的内部包元数据、未被应用显示消费的诊断接口/缓存或 Dev 技术标记；具体消费者及保持策略见 S6-theme-diagnostic-boundaries.md，禁止语言变化修改原始数据'
    s6_runtime = s6_call(r)
    if file=='tools/vst_scanner/src/ProbeMain.cpp' and s6_runtime and s6_runtime[0]=='translate' and re.search(r'error\s*=\s*\{\s*"[^"\n]+"\s*\+',ctx):
        return 'exclude','','Probe English 传输回退的拼接前缀，不是完整翻译模板；当前 Scanner 通过同一稳定 key/原始参数显示带 %1 的完整模板（该调用点另列），旧消费端仍接收组装后的完整 English；不将前缀误当成无占位符模板'
    if s6_runtime:
        return s6_runtime[0], s6_runtime[2] if s6_runtime[0]=='translate' else '', (
            'S6 实际 English 显示基线；key 与四语言草稿见核对表，人工审校仍待用户意见'
            if s6_runtime[0]=='translate' else 'S6 稳定语言 key；插件、工程及进程身份保持原样')
    if file in ('src/vst/src/I18n.cpp','src/vst/src/I18n.hpp'):
        return 'exclude','','VST 可选 i18n ABI/域/绑定属性/descriptor 协议；可见 English 在业务调用点及完整语言包另列'
    if file=='tools/vst_scanner/include/WinJACKNexus/VstScanner/Language.hpp':
        return 'exclude','','扫描器启动语言注册的资源/schema/域/目录身份及仅写诊断日志的初始化 detail；窗口显示和扫描消息另列'
    if file in ('tools/vst_scanner/src/ProbeMain.cpp','tools/vst_scanner/src/Scanner.cpp') and text in ('_key','_args'):
        return 'exclude','','S6 Probe IPC 显示元数据字段后缀；稳定协议身份，完整 English 回退和对应译文另列'
    if file=='tools/vst_scanner/src/Scanner.cpp' and re.search(r'key\s*==\s*QStringLiteral\(',ctx):
        probe_keys = json.loads((ROOT/'tools/vst_scanner/locate/manifest.json').read_text(encoding='utf-8'))['requiredKeys']['vstscanner']
        if text.startswith('probe.') and text in probe_keys:
            return 'exclude','','Probe 项目错误显示 key 白名单比较，禁止比较译文；scannerText 的实际 English 模板另列翻译'
    if file=='src/ui/src/ThemeManagementDialog.cpp' and text in (
            'Imported image','Temporary image resource for theme export',
            'Imported font','Temporary font resource for theme export'):
        return 'exclude','','importImage/importFont 临时资源载体的 metadata；仅将资源及 token 合并到当前主题，完整/片段导出使用 editedMetadata() 的用户内容，禁止语言切换改写包内容；实际控件及错误仍另列翻译'
    if file=='tools/vst_scanner/src/Scanner.cpp' and text in ('Instruments','Effects') and 'categoryDirectory' in ctx:
        return 'exclude','','已交付扫描目录结构身份，直接参与 info 文件输出路径；窗口分类说明由 vstscanner/category、effect、instrument 独立翻译'
    if file=='tools/vst_scanner/src/MainWindow.cpp' and text=='%1-bit' and 'plugin.bits' in ctx:
        return 'exclude','','位数与标准 bit 技术单位的拼接；表头由 vstscanner/bits 翻译，实际 32/64 位及数值格式保持'
    if file=='src/vst/src/Library.cpp' and (
            ('kNodeName =' in ctx and text=='VST Plugin')
            or (text=='Loads one VST plugin through a native VSTProcess sandbox.' and '"Loads' in ctx)):
        return 'exclude','','VST 原 NodeInfo/导出 SDK 身份；可见节点名称由 optional descriptor 的 vst/module.name 翻译，SDK 描述不改写'
    if file=='Develop/NativeFx/ExampleNativeFx.cpp' and (
            'return {' in ctx or 'WjnFxMeta meta' in ctx or 'wjn_get_node_name' in ctx
            or text=='Fixed eight-channel gain example'):
        return 'exclude','','社区 Native Fx 既有 SDK 元数据/导出身份；可见名称由 example.nativefx/name 单独翻译，身份值不改写'
    if file=='Develop/Node/ExampleNode.cpp' and text in (
            'Example Node','Tools','Minimal Node API implementation for integration validation.'):
        return 'exclude','','社区 Node 既有 NodeInfo/导出身份；宿主使用 optional descriptor 的 example.node/name 与 description 翻译可见副本'
    s5_runtime=s5_call(r)
    if s5_runtime:
        return s5_runtime[0],s5_runtime[2] if s5_runtime[0]=='translate' else '',('S5 English 显示基线；实际 key、四语言草稿及待审校状态见核对表' if s5_runtime[0]=='translate' else 'S5 稳定语言 key；参数、预设索引及模块身份保持原样')
    if file in ('src/native_fx/src/I18n.cpp','src/native_fx/include/WinJACKNexus/NativeFx/I18n.hpp'):
        return 'exclude','','每 DLL 可选 i18n ABI/descriptor/locate/属性/域身份；实际 English 显示基线在模块调用点列出'
    if file.startswith('src/native_fx/effects/') and (file.endswith('/Meta.hpp')
            or (file.endswith('/Logic.inc') and re.search(r'return\s*\{',ctx) and '"Native Fx"' in ctx)):
        return 'exclude','','Native Fx 原始 Meta/NodeInfo 的 SDK 身份、分类、版本与说明；可见名称及说明通过同模块 module.name/module.description 独立翻译'
    s4_runtime = s4_call(r)
    if s4_runtime:
        return s4_runtime[0], s4_runtime[2] if s4_runtime[0]=='translate' else '', ('S4 English 显示基线；实际 key、四语言草稿及待审校状态见核对表' if s4_runtime[0]=='translate' else 'S4 稳定语言 key；显示值另列，禁止用于改写工程或端口身份')
    if file=='src/ui/src/LocalizedMixerErrors.hpp' and re.search(r'[\u3400-\u9fff]',text):
        return 'exclude','','旧模型诊断匹配身份或截取后缀；GUI 副本由 mixer/error.* 翻译，模型和日志原文不改写'
    if file=='src/meter/mod/src/MeterCard.cpp' and text in ('预设名称或参考值无效','无法创建目录：'):
        return 'exclude','','预设写入 API 的旧诊断匹配身份；窗口以 preset.invalid/preset.directoryFailure 翻译显示副本，保留原始路径'
    if file=='src/ui/src/FxSettingsWindow.cpp' and text in ('All','In','Out','In\\n','Out\\n'):
        return 'exclude','','All 为声道选择的内部身份；In/Out 为端点技术缩写。显示 All 与端点说明分别由 channel.all、endpoint.* 翻译，身份不用于显示翻译结果判断'
    if file in ('src/meter/mod/Library.cpp','src/meter/probe/Library.cpp') and text in {'Meterbridge','Meter','Top-level Meterbridge window module.','Meter Mono','Single-channel Meterbridge probe.','Meter Stereo','Two-channel Meterbridge probe.','Meter 5.1','5.1-channel Meterbridge probe.','Meter 7.1','7.1-channel Meterbridge probe.','Meter Custom','Custom-channel Meterbridge probe.','Host-drawn card background for this meter probe type'}:
        return 'exclude','','既有 NodeInfo/导出名称/分类或主题开发说明身份；节点库名称和描述已有 S3 patchbay/node.meter* 显示翻译，主题开发说明不在操作 UI 呈现'
    if file=='src/ui/src/AuxSettingsWindow.cpp' and text in ('Pre','Post'):
        return 'exclude','','Pre/Post 为紧凑前/后推子技术标记；操作说明独立翻译，路由枚举保持原样'
    if file=='src/ui/src/MixerWindow.cpp' and text in ('MASTER','POST','IN','MUTE'):
        return 'exclude','','计划术语表保留的总线/前后推子/静音技术标记；菜单操作说明独立翻译，既有 SEL objectName 和通道身份保持原样'
    if file=='src/ui/src/MixerWindow.cpp' and text in ('Main','Sub Mix') and ('const std::array<QString,4> tabs' in ctx or 'selTab%1' in ctx):
        return 'exclude','','SEL 页签对象身份数组；可见标签由 mixer/tab.main、section.subMix 独立绑定'
    s3_runtime=s3_call(r)
    if s3_runtime:
        return s3_runtime[0],s3_runtime[2] if s3_runtime[0]=='translate' else '',('S3 English 显示基线；实际 key、四语言值及待人工审校状态见核对表' if s3_runtime[0]=='translate' else 'S3 稳定语言 key，显示基线另列')
    if file=='src/patchbay/mod/src/Library.cpp' and text=='interface.' and 'type.toUtf8()' in ctx:
        return 'exclude','','S3 接口类型构造语言 key 的稳定前缀；四个完整 key 和显示值在实际语言包中逐项记录'
    if file=='src/patchbay/mod/src/Library.cpp' and text=='VST Plugins / ':
        return 'exclude','','VST 扫描结果的稳定分类路径前缀，供 splitCategoryPath/模块身份使用；节点库 category.vst 和生成的效果/乐器目录另行翻译，用户目录保留'
    if file=='src/patchbay/mod/src/Library.cpp' and text=='%1\\n%2\\n%3 %4-bit\\n%5':
        return 'exclude','','VST 第三方名称/厂商/版本/路径及架构 bit 的 tooltip 拼接格式；原始元数据和架构技术标记保留，无项目操作说明'
    if file=='src/patchbay/mod/src/Library.cpp' and text=='JACK multi-client patchbay module.':
        return 'exclude','','接线盘宿主内部 NodeInfo 的旧 SDK 模块说明；当前节点库不扫描 EXE 同级宿主 DLL、UI 不呈现此字段，窗口操作说明由 patchbay 域独立提供'
    if file=='src/adapter/src/Library.cpp' and text in ('WASAPI In','WASAPI Out','System MIDI In','System MIDI Out','Windows Native Driver','Audio Adapter'):
        return 'exclude','','旧 Node API 导出的模块名称/分类身份，托盘设备类型仍按原始值识别；S3 使用 adapter/node.* 和 patchbay/category.* 单独翻译显示'
    if file=='src/adapter/src/Library.cpp' and text.startswith('Host-drawn card background for this adapter node type'):
        return 'exclude','','主题 descriptor 开发者 token 说明；当前业务 UI 不呈现此说明，不是适配器操作文案'
    if file=='src/patchbay/mod/src/Library.cpp' and text in ('根目录','Audio Adapter','Windows Native Driver','Sub Graph','Sub Graph Interface','Meter','Digital Mixer','Patchbay','jackclient','VST Plugins','Instruments','Effects','External Audio Client','DAW Bridge'):
        return 'exclude','','S3 保留的分类/创建哨兵/目录/JACK 客户端身份；分类显示由独立语义 key 生成，未知/用户目录与客户名原样保留'
    if file=='src/patchbay/mod/src/PatchbayCanvas.cpp' and text in ('Sub Graph Audio In','Sub Graph Audio Out','Sub Graph MIDI In','Sub Graph MIDI Out','Sub Graph'):
        return 'exclude','','S3 既有 JACK 接口端口名称或子图创建哨兵，参与端口注册/旧工程读取；节点库类型说明和新子图默认显示名另列翻译'
    if file.endswith('/LocalizedText.hpp'):
        return 'exclude','','S3 通用绑定的属性/域/key 身份与空 English 兜底参数；实际显示文字在业务调用点和语言包中列出'
    runtime=host_call(r)
    if runtime:
        return runtime[0],runtime[1] if runtime[0]=='translate' else '',('S2 已接入 host 域的 English 字面兜底，实际四语言包及审校见核对表' if runtime[0]=='translate' else 'S2 稳定运行时语言 key，显示值另列')
    if file.endswith('/HostI18n.cpp'):
        return 'exclude','','宿主语言/描述符/Qt 资源/属性身份；语言自称固定原样，自动项说明独立翻译'
    if file=='src/app/main.cpp' and 'QCoreApplication::translate("QFileDialog","&Choose")' in ctx and text in ('QFileDialog','&Choose'):
        return 'exclude','','Qt 标准目录选择器上下文及原文，由已部署/内嵌的同版 Qt QM 翻译；不重复纳入宿主语言包'
    if file.endswith('/I18nManager.cpp') and text != 'Unknown':
        return 'exclude', '', 'S1 核心 schema/域/语言/路径/设置身份或稳定 API 校验 detail；用户提示外层在 S2 独立翻译'
    quoted = re.escape('"'+text+'"')
    if re.search(r'QSettings|\bsettings\(',ctx) and text in ('WinJACKNexus','Application','Jackd','Meterbridge','Patchbay'):
        return 'exclude', '', 'QSettings 组织/组身份；显示名称单列'
    if re.search(r'MetricDefinition\s*\{\s*QStringLiteral\('+quoted,ctx):
        return 'exclude', '', 'MetricDefinition 首字段是 objectName，测量显示标签是独立第二字段'
    if file.endswith('MixerWindow.cpp') and 'selTab%1' in ctx and text in ('Main','AUX','FX','Sub Mix'):
        return 'exclude', '', 'SEL 页签 objectName 后缀；标签的显示源另列并须分离稳定 ID'
    if text == 'DigitalMixer' or re.fullmatch(r'(?:Mic|Line|Bus|USB|Ch) \d+',text) or text == '*.vstinfo':
        return 'exclude', '', '已有示例名称/技术声道/客户名或扫描文件扩展规则'
    if re.search(r'\b(?:clientName_|clientId_)\s*(?:=|\{)\s*'+quoted,ctx) or re.search(r'clientOpen\(\s*'+quoted,ctx):
        return 'exclude', '', 'JACK 客户端名称身份，热切换不改变'
    if re.search(r'WJN_LOG_|qFatal|qWarning|qDebug|qInfo|Logger::|fprintf|printf\(',ctx):
        return 'exclude', '', '诊断日志事件/原始详情；UI 外层提示另行翻译'
    override = decisions.get(r['id'], decisions.get('text:'+text, decisions.get('text:'+text.strip())))
    if override:
        return override['kind'], override.get('key',''), override['reason']
    if r['evidence_class'] == 'raw-structured-review':
        if not text.lstrip().startswith('{'):
            return 'exclude', '', '原始正则表达式/样式语法，不含项目可见自然语言'
        return 'structured', '', '原始结构化描述符：逐字段提取显示名/说明，协议结构保留'
    if re.search(r'#\s*include',ctx) or '#pragma comment' in ctx:
        return 'exclude', '', '编译依赖/链接库标识'
    if not text or not re.search(r'[A-Za-z\u3400-\u9fff]', text):
        return 'exclude', '', '空值、符号、纯数值或无自然语言的格式串'
    if re.fullmatch(r'#[a-fA-F0-9]{3,8}',text):
        return 'exclude', '', '颜色机器格式'
    if file.endswith('.rc'):
        return 'exclude', '', 'Windows 版本资源/产品元数据，不是应用操作界面'
    if 'qFatal' in ctx or 'qWarning' in ctx or 'qDebug' in ctx or 'qInfo' in ctx or 'Logger::' in ctx or 'fprintf' in ctx or 'printf(' in ctx:
        return 'exclude', '', '诊断日志事件/原始详情；UI 外层提示另行翻译'
    if re.search(r'(?:setObjectName|setProperty|property|GetProcAddress|resolve)\s*\([^;]*?'+quoted,ctx) and not re.search(r'setWindowTitle\([^;]*?'+quoted,ctx):
        return 'exclude', '', '对象/属性/导出稳定身份'
    if 'MeterLoudnessPresetCatalog.hpp' in file and text != '自定义参考':
        return 'exclude', '', '既有响度标准名称与稳定预设 ID，计划书要求保留原样'
    if re.search(r'[\u3400-\u9fff]',text):
        return 'pending', '', '中文自然语言需分配语义 key 或明确排除'
    if text in ('Microsoft YaHei UI','Segoe UI','Arial','Consolas','monospace','sans-serif'):
        return 'exclude', '', '字体族身份，使用系统 fallback，不翻译字体名'
    if re.match(r'^(?:[a-zA-Z]:|:/|https?://|\\\\)',text) or re.search(r'\.(?:dll|exe|json|css|wjnskin|wjnproj|csv|png|ttf|otf|wav|log|qm|svg|ico)(?:$|[/\\])',text,re.I):
        if '(*.' not in text:
            return 'exclude', '', '文件/资源/网址路径或加载身份'
    if re.search(r'QRegularExpression|std::regex|setStyleSheet',ctx) or re.search(r'^(?:Q\w+\s*\{|[\w-]+\s*:.*;)',text):
        return 'exclude', '', '匹配表达式或样式语法'
    if re.match(r'^(?:--?|__wjn_|WinJACKNexus[-.]|WJN_|wjn_)',text):
        return 'exclude', '', '命令参数、环境键、内部名称或导出符号'
    if re.match(r'^\s*-\w',text) or text.startswith(('application/','assets/','build/','/','.')) or re.search(r'\\"(?:name|author|category|dependencies|description|version|ok|error|inputs|outputs|reply|active|stopping|inputLabels|outputLabels|midiInputs)',text):
        return 'exclude', '', '命令参数、MIME、路径、扩展名或 JSON 序列化片段'
    if text.startswith(':root') or re.search(r'background-color:|border:|border-.*radius:|font-size:|font-weight:|padding:',text) or re.fullmatch(r'%[a-z][a-z-]*%',text):
        return 'exclude', '', 'CSS 语法/主题模板占位 token'
    if re.fullmatch(r'[\s%\d.,:+±−∞kxf/Ln-]*(?:dB(?:FS|TP|/Oct)?|Hz|kHz|ms|s|FPS|px|k|x)?(?:[\s%\d.,:+±−∞kxf/Ln⌄-]*)',text) or text in ('(ms)','%v / %m','0123456789ABCDEF'):
        return 'exclude', '', '技术数值、单位、刻度或进度格式，保持原样'
    if re.fullmatch(r'\d+k|\d+ FPS',text) or (text.startswith('%') and re.fullmatch(r'(?:%\d|\\n|\s)+',text)):
        return 'exclude', '', '数值刻度/技术显示拼接格式'
    if re.fullmatch(r'[a-z]+',text):
        return 'exclude', '', '已检查的短小写词：字段/枚举/token/协议身份；显示名称单列'
    if text in ('control-button-','control-accent-%1'):
        return 'exclude', '', '共享 SEL 主题 Token 前缀/参数化角色身份，不是显示文字'
    if text in ('background-','custom-','CH ','Input %1','vst2','vst3') or re.fullmatch(r'\s*(?:LUFS|dBTP|dBFS|dB)\s*',text) or text in ('\\n','*.vst3info'):
        return 'exclude', '', '主题/预设/协议/默认保存名称或机器格式身份'
    if re.search(r'^Q(?:Frame|Label|Widget)[#\[]|^(?:rgb|rgba|url|var)\(',text) or re.search(r'^(?:chart-series|control-knob|fader|node-wire|overview-title|selTab|meterCard|meterHistory|meterSettings|lufsPreset|layout/|theme/|tests/)',text):
        return 'exclude', '', '样式/对象/测试位置/主题 token 模板身份'
    if text in ('Application','Jackd','Meterbridge','Patchbay','Contents','Resources','ProgramFiles','ProgramW6432','SystemRoot','VSTPlugins','VSTPluginMain','WinJACKNexusVstEditor','WJNServerProbe','ExampleGain','DeEsser','GraphicEQ','ParametricEQ','WinJACKNexus'):
        return 'exclude', '', '产品名、文件夹、设置组、类名、客户名或系统身份'
    if text in ('Native Fx','Audio Module Class','Pro Audio','WinJACKNexus Node API','WinJACKNexus Default','JACK Out 1/2') or re.fullmatch(r'(?:Analog|Bus|Line|USB) In \d+',text):
        return 'exclude', '', '固定分类/协议/系统任务/产品名或已有保存的示例通道名称'
    if re.fullmatch(r'(?:L|R|C|M|G|S|Lb|Rb|Ls|Rs|Lrb|Rrb|Lw|Rw|Ltf|Rtf|Ltm|Rtm|Ltr|Rtr|[MUT][+-]\d{3}|[LR]\d+%)\s*',text):
        return 'exclude', '', '技术声道/声像标记或效果图标缩写，保持原样'
    if re.search(r'^(?:aux:|sub:|input:|tool:|internal:|Local\\|Meter-|meter-probe-|meter-preview-|MIDI-|WASAPI-|Ch_|in_|midi_in_|vst_|subgraph-|native-fx-|native_fx/|module-|host/|node/|load-preset |save-preset |open-editor|quit\\n|status\\n)',text):
        return 'exclude', '', '路由/模块/客户/端口/进程命令与资源稳定身份'
    if text in ('AUX %1','CH %1','SUB %1','SUB%1','Main L/R','Ch ','HH:mm:ss','HH:mm:ss.zzz','yyyyMMdd_HHmmss_zzz','yyyyMMdd-HHmmss','DSP %1% · XRun %2','LRA (LU)','LUFS'):
        return 'exclude', '', '默认已持久化名称/端口前缀、日期机器格式或技术数值标识'
    if file.endswith('Logger.cpp') or file.startswith('tools/vst_process/') or text.startswith(('[--','> --','Usage:','Qt category=')):
        return 'exclude', '', '无项目 GUI 的进程诊断/命令帮助/日志格式；保持稳定英文'
    if text.startswith('localTime,') or text in ('#RRGGBB / rgb(...) / rgba(...)','50% / 12px'):
        return 'exclude', '', 'CSV 机器字段或输入语法示例，说明另行翻译'
    if re.fullmatch(r'[a-z][a-z0-9]*(?:[-_.][a-z0-9]+)+',text):
        return 'exclude', '', '机器 token、域、序列化键或稳定 ID（周边显示说明单列）'
    if re.fullmatch(r'[a-z]+(?:[A-Z][a-zA-Z0-9]*)+',text):
        return 'exclude', '', 'camelCase 字段、对象、属性或协议身份'
    if re.search(r'^(?:jack_|mod_|wjn_|WJN_|[A-Z][A-Z0-9_]+$)',text):
        return 'exclude', '', '导出/模块/缩写技术标识，说明另行翻译'
    # JSON map keys and lookups, not their displayed value arguments.
    escaped = re.escape('"'+text+'"')
    if re.search(r'(?:\.value|\.contains|\.insert|\.remove|\.take|\.find|\.at|\bread(?:Number|Int|Float|Bool|String)?|\bget(?:Int|String|Bool)|\bsetValue|\bqEnvironmentVariable|\bqgetenv)\s*\(\s*(?:QStringLiteral\()?'+escaped,ctx):
        return 'exclude', '', '结构字段/设置键/查询身份；对应显示值单列'
    if re.search(r'\b(?:themeTokenColor|themeColor|themeMetric|themeBool|themeNumber|themeString|colorToken|metricToken|tokenColor)\s*\(',ctx):
        return 'exclude', '', '主题作用域/token 标识'
    if re.search(r'[A-Za-z]{3,}\s+[A-Za-z]{2,}',text):
        return 'translate', 'text.'+slug(text), '项目拥有的自然语言显示/错误模板；保留其中技术参数与标识'
    return 'pending', '', '短英文或格式模板需逐项确认'

def module_domain(r):
    f = r['file']
    if f.startswith('src/native_fx/effects/'):
        return 'nativefx.'+f.split('/')[3].replace('_','-')
    if f == 'src/native_fx/src/NativeFxApi.cpp' and 'static const char* names[]' in r.get('context',''):
        lines = (ROOT/f).read_text(encoding='utf-8-sig').splitlines()
        for line in reversed(lines[:int(r['line'])-1]):
            if line.startswith('#else'): return 'nativefx.reverb'
            match = re.match(r'#(?:if|elif) WJN_FX_MODULE_INDEX == (\d+)\s*$',line)
            if match:
                return 'nativefx.'+('gate','graphic_eq','deesser','compressor','limiter','delay','parametric_eq','reverb')[int(match[1])]
    if f.startswith('src/ui/'):
        name = Path(f).stem
        if name.startswith(('Theme','LocalizedTheme')): return 'theme'
        if name in ('MixerWindow','MixerOverview','MixerPolarPlot'): return 'mixer'
        if name == 'AuxSettingsWindow': return 'aux'
        if name.startswith('Fx'): return 'fxhost'
        if name == 'TrayController': return 'host'
    if f.startswith('src/common/'):
        name = Path(f).stem
        if name.startswith(('DigitalMixer','AuxMixer')): return 'mixer'
        if name.startswith(('Meter','Loudness')): return 'meterbridge'
    return r['domain']

def main():
    decisions = read_decisions()
    rows = list(csv.DictReader((OUT/'S0-source-literals.csv').open(encoding='utf-8-sig',newline='')))
    classified, keys, excluded, pending, structured, identity, fields = [], [], [], [], [], [], []
    for r in rows:
        kind, key, reason = classify(r,decisions)
        runtime = host_call(r)
        s3_runtime = s3_call(r)
        s4_runtime = s4_call(r)
        s5_runtime = s5_call(r)
        s6_runtime = s6_call(r)
        domain = s6_runtime[1] if s6_runtime else s5_runtime[1] if s5_runtime else s4_runtime[1] if s4_runtime else 'host' if runtime else s3_runtime[1] if s3_runtime else module_domain(r)
        # The source component separates distinct meanings until explicit sharing is reviewed.
        component = re.sub(r'[^a-z0-9]+','.',Path(r['file']).stem.lower()).strip('.')
        record = dict(r,disposition=kind,proposed_key=key,notes=reason,translation_domain=domain)
        classified.append(record)
        if source_use(r) == 'identity-guard':
            identity.append({'source_id':r['id'],'domain':domain,'file':r['file'],'line':r['line'],
                'source':r['literal'],'context':r['context'],'classification':kind,
                'action':'分离稳定身份与显示；不得将比较常量替换为译文' if kind=='translate' else '保持稳定身份；显示文字另行翻译'})
        if kind == 'translate':
            lines = (ROOT/r['file']).read_text(encoding='utf-8-sig').splitlines()
            pos = int(r['line'])-1
            keys.append({'source_id':r['id'],'domain':domain,'key':key if runtime or s3_runtime or s4_runtime or s5_runtime or s6_runtime else component+'.'+key,
                'source':r['literal'],'file':r['file'],'line':r['line'],'control_context':r['context'],
                'usage':source_use(r),
                'source_window':'\n'.join(f'{i+1}: {lines[i]}' for i in range(max(0,pos-2),min(len(lines),pos+3))),
                'placeholders':','.join(re.findall(r'%(?:L?\d+|n)|\{[A-Za-z_]\w*\}',r['literal'])),
                'layout':'自绘/固定 rect：按调用点布局与 hit rect 测量，超长省略+完整 tooltip' if ('/effects/' in r['file'] or 'drawText' in r['context'] or 'MixerOverview' in r['file']) else '原生菜单/共享布局按字体测量；保留编辑值、焦点与完整 accessibleName',
                'review':('S6' if s6_runtime else 'S5' if s5_runtime else 'S4' if s4_runtime else 'S2' if runtime else 'S3')+' 实际运行时 key；四语言草稿已提供，待母语/领域审校' if runtime or s3_runtime or s4_runtime or s5_runtime or s6_runtime else 'S0 key 提案；四语言译文和母语审校尚未实施','notes':reason})
        elif kind == 'exclude':
            excluded.append({'source_id':r['id'],'domain':domain,'source':r['literal'],
                'file':r['file'],'line':r['line'],'context':r['context'],'reason':reason,'preservation':'原样；周边项目说明另行翻译'})
        elif kind == 'pending': pending.append(record)
        elif kind == 'structured':
            structured.append(record)
            parsed = json.loads(r['literal'])
            for field,value,display in descriptor_fields(parsed):
                fields.append({'source_id':r['id'],'field':field,'source':value,'file':r['file'],
                    'line':r['line'],'domain':domain,'key':component+'.descriptor.'+field if display else '',
                    'disposition':'translate' if display else 'exclude',
                    'reason':'项目描述符显示名称/说明，身份字段仍原样' if display else '描述符结构/域/类型/token/默认样式身份'})
    write_csv('S0-classified-literals.csv',classified)
    write_csv('S0-keys.csv',keys)
    write_csv('S0-exclusions.csv',excluded)
    write_csv('S0-pending.csv',pending,list(classified[0]))
    write_csv('S0-structured.csv',structured,list(classified[0]))
    write_csv('S0-descriptor-fields.csv',fields)
    write_csv('S0-identity-uses.csv',identity)
    resources, resource_files = [], []
    for name in ('src/ui/resources/ui.qrc','src/default_theme/resources/default_theme.qrc',
                 'src/patchbay/mod/resources/patchbay.qrc','Docs/Theme-v1-Registry.json',
                 'themes/template/winjacknexus-default.wjnskin','themes/template/meta.json'):
        data = (ROOT/name).read_bytes()
        resource_files.append({'file':name,'sha256':hashlib.sha256(data).hexdigest()})
    package_directories = ['locate','Develop/Node/locate','Develop/NativeFx/locate',
                           'src/vst/locate','tools/vst_scanner/locate']
    package_directories.extend(f'src/native_fx/effects/{effect}/locate'
        for effect in ('gate','graphic_eq','deesser','compressor','limiter','delay','parametric_eq','reverb'))
    for directory in package_directories:
        for path in sorted((ROOT/directory).glob('*.json')):
            name = path.relative_to(ROOT).as_posix()
            data = path.read_bytes()
            resource_files.append({'file':name,'sha256':hashlib.sha256(data).hexdigest()})
            document = json.loads(data)
            def package_fields(value, field=''):
                if isinstance(value,dict):
                    for key,item in value.items(): yield from package_fields(item,field+'.'+key if field else key)
                elif isinstance(value,list):
                    for index,item in enumerate(value): yield from package_fields(item,field+'.'+str(index))
                elif isinstance(value,str): yield field,value
            for field,value in package_fields(document):
                parts = field.split('.')
                display = path.name != 'manifest.json' and parts[0]=='domains'
                domain,key = 'i18n.metadata',''
                if display:
                    # Domains and keys themselves contain dots: use the object
                    # hierarchy, not a split of the flattened field path.
                    for candidate,entries in document['domains'].items():
                        prefix='domains.'+candidate+'.'
                        if field.startswith(prefix):
                            domain=candidate
                            suffix=field[len(prefix):]
                            key=next(k for k in entries if suffix==k or suffix in (k+'.one',k+'.other'))
                            break
                resources.append({'file':name,'field':field,'source':value,'domain':domain,'key':key,
                    'disposition':'translate' if display else 'exclude',
                    'reason':'S1 实际语言包显示值；缺译/审校状态见根目录翻译核对表' if display else 'S1 包/schema/域/key/语言/版本映射身份'})
    for file, data, runtime in (
        ('themes/template/winjacknexus-default.wjnskin!meta.json',
         json.loads(zipfile.ZipFile(ROOT/'themes/template/winjacknexus-default.wjnskin').read('meta.json')),True),
        ('themes/template/meta.json',json.loads((ROOT/'themes/template/meta.json').read_text(encoding='utf-8')),False)):
        for field,value in data.items():
            if isinstance(value,str):
                display = runtime and field == 'description'
                resources.append({'file':file,'field':field,'source':value,'domain':'theme',
                    'key':'identity.defaultDescription' if display else '',
                    'disposition':'translate' if display else 'exclude',
                    'reason':'发行内置默认主题只读说明的实际 theme/identity.defaultDescription 显示副本；编辑/导出 metadata 保持原文' if display else '作者主题名称、产品专名、用户模板示例/元数据或主题身份，原样保留；只读默认说明另列独立翻译'})
    registry = json.loads((ROOT/'Docs/Theme-v1-Registry.json').read_text(encoding='utf-8'))
    for token in registry['tokens']:
        for field in ('name','ownerScope','type','default','description'):
            if field in token:
                resources.append({'file':'Docs/Theme-v1-Registry.json','field':token['name']+'.'+field,
                    'source':str(token[field]),'domain':'theme','key':'','disposition':'exclude',
                    'reason':'当前 UI 只显示 token 名；registry 开发者语义说明/样式契约，不是已呈现的帮助文字' if field=='description' else '冻结主题 token/类型/作用域/默认样式身份'})
    write_csv('S0-resource-fields.csv',resources)
    (OUT/'S0-resource-manifest.json').write_text(json.dumps(resource_files,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    catalog = {}
    for item in keys:
        if item['usage'] == 'identity-guard': continue
        catalog.setdefault((item['domain'],item['key']),[]).append(item)
    catalog_rows=[]
    for (domain,key),items in sorted(catalog.items()):
        catalog_rows.append({'domain':domain,'key':key,'sources':json.dumps(sorted({i['source'] for i in items}),ensure_ascii=False),
            'locations':'\n'.join(sorted({i['file']+':'+i['line'] for i in items})),
            'placeholders':items[0]['placeholders'],'layout':items[0]['layout'],
            'translation_status':'未实施（S0 清单）','semantic_review':'Gate/Limiter 疑点见 S0-glossary/identity-risks；其它按调用上下文；译文仍需领域/母语审校'})
    for item in fields+resources:
        if item['disposition']=='translate':
            location=item['file']+(':'+item['line'] if 'line' in item else '!'+item['field'])
            existing=next((row for row in catalog_rows if (row['domain'],row['key'])==(item['domain'],item['key'])),None)
            if existing:
                existing['sources']=json.dumps(sorted(set(json.loads(existing['sources']))|{item['source']}),ensure_ascii=False)
                existing['locations']='\n'.join(sorted(set(existing['locations'].split('\n'))|{location}))
            else:
                catalog_rows.append({'domain':item['domain'],'key':item['key'],'sources':json.dumps([item['source']],ensure_ascii=False),
                    'locations':location,'placeholders':','.join(re.findall(r'%(?:L?\d+|n)|\{[A-Za-z_]\w*\}',item['source'])),
                    'layout':'元数据/语言包显示，布局度量并保留完整 tooltip',
                    'translation_status':'语言包项见根目录 i18n翻译核对.md；其它未实施（S0 清单）',
                    'semantic_review':'描述符/内置主题/语言包的显示角色；身份字段不变'})
    write_csv('S0-key-catalog.csv',catalog_rows)
    manifest = json.loads((OUT/'S0-source-manifest.json').read_text(encoding='utf-8'))
    coverage=[]
    for entry in manifest['files']:
        subset=[r for r in classified if r['file']==entry['file']]
        counts=Counter(r['disposition'] for r in subset)
        coverage.append({'file':entry['file'],'sha256':entry['sha256'],'literal_occurrences':len(subset),
            **{k:counts[k] for k in ('translate','exclude','structured','pending')},
            'domain':','.join(sorted({r['translation_domain'] for r in subset})) if subset else __import__('scan_s0').domain(entry['file']),
            'decision':'显示文字/元数据/错误 + 固定协议/技术项分别对账' if subset else '已扫描，无字符串 UI 源'})
    write_csv('S0-file-coverage.csv',coverage)
    print(json.dumps(Counter(r['disposition'] for r in classified),ensure_ascii=False))
    print('Pending unique:',len({r['id'] for r in pending}))

if __name__ == '__main__': main()
