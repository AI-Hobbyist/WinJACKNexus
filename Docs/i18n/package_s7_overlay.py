"""Create and verify the reviewable incremental overlay; no UI/module execution."""
import argparse
import hashlib
import json
import re
import struct
import subprocess
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Docs/i18n'
BIN = ROOT / 'build/release/bin'
DUMPBIN = Path('D:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64/dumpbin.exe')


def digest(value):
    return hashlib.sha256(value).hexdigest()


def machine(path):
    value = path.read_bytes()
    assert value[:2] == b'MZ'
    offset = struct.unpack_from('<I', value, 0x3c)[0]
    assert value[offset:offset + 4] == b'PE\0\0'
    return struct.unpack_from('<H', value, offset + 4)[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--review-deferred', action='store_true', help='Deliver the overlay under the user instruction to review later.')
    deferred = parser.parse_args().review_deferred
    human_review = 'deferred by user: 直接继续，审核后续再搞' if deferred else 'pending user four-language/audio-domain review'
    inventory = json.loads((OUT / 'S7-resource-inventory.json').read_text(encoding='utf-8'))
    assert inventory['status'] == 'RESOURCE_INVENTORY_PREPARED_NOT_RELEASE_ACCEPTED'
    members = {}
    for item in inventory['runtime_candidates']:
        path = ROOT / item['file']
        value = path.read_bytes()
        assert digest(value) == item['sha256'] and len(value) == item['bytes']
        members[item['destination']] = value
    for item in inventory['resource_files']:
        path = ROOT / item['deployed']['file']
        value = path.read_bytes()
        assert digest(value) == item['deployed']['sha256'] and len(value) == item['deployed']['bytes']
        if 'source' in item:
            assert (ROOT / item['source']['file']).read_bytes() == value
        assert item['destination'] not in members
        members[item['destination']] = value
    included = {Path(name).name.lower() for name in members if name.endswith('.dll')}
    dependencies = []
    qt = set()
    for item in inventory['runtime_candidates']:
        path = ROOT / item['file']
        expected = 0x14c if item['destination'] == 'Vst Scanner Probe32.exe' else 0x8664
        assert machine(path) == expected, item['destination']
        result = subprocess.run([str(DUMPBIN), '/dependents', str(path)], capture_output=True)
        assert result.returncode == 0, result.stderr.decode(errors='replace')
        names = re.findall(r'^\s+([\w.-]+\.dll)\s*$', result.stdout.decode(errors='replace'), re.M | re.I)
        assert names
        entries = []
        for name in names:
            lower = name.lower()
            if lower in included:
                kind = 'included project runtime'
            elif lower.startswith('qt6'):
                assert not lower.endswith('d.dll'), name
                deployed = BIN / name
                sdk = Path('C:/Qt/6.10.3/msvc2022_64/bin') / name
                assert deployed.read_bytes() == sdk.read_bytes(), name
                kind = 'existing-install Qt 6.10.3 Release prerequisite'
                qt.add(name)
            elif lower.startswith(('msvcp140', 'vcruntime140')):
                kind = 'existing-install MSVC redistributable prerequisite (x86 for Probe32, x64 otherwise)'
            else:
                assert lower.startswith('api-ms-win-') or lower in {'kernel32.dll', 'user32.dll', 'ole32.dll', 'shell32.dll', 'avrt.dll', 'winmm.dll'}, name
                kind = 'Windows system prerequisite'
            entries.append({'name': name, 'resolution': kind})
        dependencies.append({'file': item['destination'], 'architecture': 'x86' if expected == 0x14c else 'x64', 'direct_and_delay_imports': entries})
    # Document dynamic dependencies without claiming the static import scan covers them.
    prerequisites = {'Qt': sorted(qt), 'Qt_platform': 'existing platforms/qwindows.dll from the same Qt 6.10.3 Release installation',
                     'MSVC': 'compatible installed x64 and x86 redistributables; no redistributable install is performed',
                     'dynamic_runtime': ['existing wjn_default_theme.dll and its theme runtime', 'existing VSTProcess32.exe/VSTProcess64.exe and their architecture-specific dependencies', 'existing JACK libjack64.dll/libjack.dll for audio use'],
                     'scope': 'Incremental overlay for a compatible existing WinJACKNexus desktop installation. Static import resolution is checked; dynamic loading and deployment execution are not tested.'}
    metadata = {'format_version': 1, 'package_kind': 'incremental Release overlay' if deferred else 'incremental Release overlay draft', 'source_commit': inventory['implementation_commit'],
                'human_review': human_review, 'release_accepted': False,
                'delivery_status': 'delivered with human review deferred' if deferred else 'draft',
                'resources': [{'module_id': m['module_id'], 'catalog_version': m['catalog_version'], 'domains': m['domains'], 'language_coverage': m['language_coverage']} for m in inventory['modules']],
                'prerequisites': prerequisites, 'files': [{'path': name, 'bytes': len(value), 'sha256': digest(value)} for name, value in sorted(members.items())],
                'not_verified': ['Scanner repaired UI, shared control additions and S6 supplemental UI behavior.', 'Remaining cold-start/language/scale combinations recorded in prior S5 evidence.', 'Installing this overlay, first-start/restart behavior or preserving an actual existing installation during deployment.'],
                'selection_protection': 'Payload contains no QSettings ini/registry data, user projects, themes, presets or background files. Existing language selection is read by the application; archive validation does not execute that path.'}
    instructions = '''WinJACKNexus i18n Release 增量覆盖包（待审校草稿）

这不是完整安装包；S6 四语言/音频领域人工审校仍待用户意见，尚未发行验收。
仅供与现有 WinJACKNexus、Qt 6.10.3 Release 和 MSVC 运行库兼容的安装使用。
Probe32 使用 x86 MSVC 运行库，其他文件使用 x64；保留既有 Qt platforms/qwindows.dll、默认主题、VSTProcess32/64 及 JACK 环境。

相对路径与程序安装根目录对应；examples 为可选开发示例，无需加载为业务节点。
计划部署时先自行退出目标安装的程序并备份受覆盖文件，再将包中同名运行时和 locate 文件覆盖到安装根目录。
本次打包没有操作或停止任何运行中的应用/音频进程，也没有覆盖用户安装。
不要删除原安装目录、主题、预设、工程或设置。包不携带 QSettings 数据，故不会主动改写保存的语言选择。
现有运行时依赖不在本增量包中；不可用作新机器的完整安装。

13 模块资源有完整 English。Node 开发示例按 S1 合同有意在三种非 English 中缺 description/greeting，以演示 English fallback；缺项未标成译文。
此前每模块单次测试结果保留其原基线。Scanner 修复后、共享控件新增显示和 S6 补漏没有新增实窗验收；不再做缩放或重复模块测试。
归档 CRC、成员、逐文件 SHA-256、PE 架构及直接/延迟导入检查仅证明包结构和静态依赖，不代表运行时/译文审校通过。
更多证据与待审校词表见仓库 Docs/i18n/S6-human-review.md、S6-supplement-verification.json 和 S7-overlay-verification.json。
'''
    if deferred:
        instructions = instructions.replace('（待审校草稿）', '（人工审校后续）').replace(
            'S6 四语言/音频领域人工审校仍待用户意见，尚未发行验收。',
            '按用户“直接继续，审核后续再搞”指令交付，四语言/音频领域人工审校后续进行。既有未验证项继续明示，不声称实窗或安装验收通过。')
    members['i18n-overlay-manifest.json'] = (json.dumps(metadata, ensure_ascii=False, indent=2) + '\n').encode('utf-8')
    members['i18n-overlay-readme.txt'] = instructions.encode('utf-8-sig')
    prefix = 'WinJACKNexus-i18n-Release-overlay-' if deferred else 'WinJACKNexus-i18n-Release-overlay-draft-'
    archive = ROOT / 'build' / (prefix + inventory['implementation_commit'][:7] + '.zip')
    # Use a fresh path to preserve any previous package.
    assert not archive.exists(), 'Archive already exists; inspect it rather than silently replacing it.'
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as package:
        for name, value in sorted(members.items()):
            assert not name.startswith('/') and '..' not in Path(name).parts
            package.writestr(name, value)
    with zipfile.ZipFile(archive) as package:
        names = package.namelist()
        assert len(names) == len(set(names)) and set(names) == set(members)
        assert package.testzip() is None
        assert all(package.read(name) == value for name, value in members.items())
        assert all(not re.search(r'(\.ts$|\.pdb$|tests|fixture|themes/|\.ini$|\.dmp$|\.log$)', name, re.I) for name in names)
        assert json.loads(package.read('i18n-overlay-manifest.json')) == metadata
    report = {'stage': 'S7', 'date': '2026-10-04', 'result': 'DELIVERY_ARCHIVE_STATIC_VERIFICATION_PASS_HUMAN_REVIEW_DEFERRED' if deferred else 'DRAFT_ARCHIVE_STATIC_VERIFICATION_PASS_NOT_RELEASE_ACCEPTED',
              'archive': {'file': str(archive.relative_to(ROOT)).replace('\\', '/'), 'bytes': archive.stat().st_size, 'sha256': digest(archive.read_bytes())},
              'payload_runtime_files': 25, 'payload_language_resources': 68, 'metadata_files': 2, 'archive_members': len(members),
              'checks': ['Existing inventory fingerprints and source/deployment equality retained.', '25 PE architectures and all direct/delay imports classified.', 'Qt prerequisites byte-match Qt 6.10.3 SDK.', 'ZIP CRC, exact member set and complete payload byte/SHA-256 comparison passed.', 'No tests, Debug binaries, TS, PDB or user settings/themes/projects in the archive.'],
              'dependencies': dependencies, 'prerequisites': prerequisites, 'module_test_runs_added': 0, 'ui_shows_added': 0,
              'installed': False, 'human_review': human_review, 'runtime_limits': metadata['not_verified'],
              'source_commit': inventory['implementation_commit'], 'files': metadata['files']}
    (OUT / 'S7-overlay-verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    (OUT / 'S7-overlay-manifest.json').write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    (OUT / 'S7-overlay-readme.txt').write_text(instructions, encoding='utf-8-sig')
    print(f'S7 archive static verification PASS: {archive.name}, {len(members)} members, {archive.stat().st_size} bytes; no module run or installation; human review: {human_review}.')


if __name__ == '__main__':
    main()
