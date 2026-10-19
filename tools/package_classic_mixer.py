"""Package the Classic Mixer overlay from the existing Release tree."""

from __future__ import annotations

import hashlib
import json
import subprocess
from pathlib import Path
import zipfile


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    runtime = root / "build/release/bin"
    output = root / "build/ClassicMixer-Release-Incremental-20261019.zip"
    files = [
        "common.dll",
        "ui.dll",
        "mod_patchbay.dll",
    ]
    for layout in (8, 16, 24):
        folder = f"nodes/classic_mixer_{layout}"
        files.append(f"{folder}/mod_classic_mixer_{layout}.dll")
        files.extend(
            f"{folder}/locate/{name}.json"
            for name in ("manifest", "en-US", "zh-CN", "ja-JP", "ko-KR")
        )
    # All runtime inputs must come from the existing Release output tree.
    payload = {name: (runtime / name).read_bytes() for name in files}
    for name, content in payload.items():
        if name.endswith(".dll"):
            pe_offset = int.from_bytes(content[60:64], "little")
            if (
                content[:2] != b"MZ"
                or content[pe_offset : pe_offset + 6] != b"PE\0\0d\x86"
            ):
                raise ValueError(f"Expected x64 PE image: {name}")
        elif name.endswith(".json"):
            json.loads(content)

    revision = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=root, text=True
    ).strip()
    source_paths = [
        "src/classic_mixer/CMakeLists.txt",
        "src/classic_mixer/Module.cpp",
        "src/classic_mixer/Theme.hpp",
        "src/common/src/ClassicMixer.cpp",
        "src/common/include/WinJACKNexus/Common/ClassicMixer.hpp",
        "src/common/include/WinJACKNexus/Common/ClassicMixerRouting.hpp",
        "src/ui/src/ClassicMixerWindow.cpp",
        "src/ui/src/ClassicMixerMessages.hpp",
        "src/ui/src/ConsoleControls.cpp",
        "src/patchbay/mod/src/Library.cpp",
        "src/ui/src/ApplicationSettings.cpp",
        "src/ui/include/WinJACKNexus/UI/ApplicationSettings.hpp",
        "src/common/src/ProcessPriority.cpp",
    ]
    source_paths.extend(
        f"src/classic_mixer/locate/{name}.json"
        for name in ("manifest", "en-US", "zh-CN", "ja-JP", "ko-KR")
    )
    manifest = {
        "kind": "incremental-overlay",
        "configuration": "Release",
        "architecture": "x64",
        "date": "2026-10-19",
        "sourceHead": revision,
        "builtFromExistingWorkingTree": True,
        "requiredBaseline": "Existing Node v4 / Qt 6.10.3 x64 WinJACKNexus installation",
        "sourceSha256": {
            name: sha256((root / name).read_bytes()) for name in source_paths
        },
        "files": [
            {"path": name, "bytes": len(content), "sha256": sha256(content)}
            for name, content in payload.items()
        ],
        "validation": {
            "audio": "offline buffers and explicit in-process JACK fixture only",
            "gui": "Windows visible windows at 100 percent; no offscreen",
            "manualAcceptance": "SKIPPED by user instruction",
            "liveJackIntegration": "NOT RUN",
        },
    }
    readme = """# Classic Mixer Release 增量替换包

适用于已有 Node v4、Qt 6.10.3 x64 的 WinJACKNexus 安装。此包不是完整包。
在方便停止音频时自行退出应用，备份旧文件，再将压缩包中的运行文件按相同相对路径覆盖到 EXE 所在目录，随后重启。
本次没有关闭用户应用或中断音频。人工安装/部署验收已跳过（用户指令）。

运行文件为六个 DLL（共享 common/ui/patchbay 与三个节点 DLL）和十五个语言 JSON。保留原工程、宿主 locate、主题、其他节点和插件。
common.dll 随包提供是因为本工作区的 ui.dll 已引用其进程优先级 API；共享 DLL 保留了实施前已有的相关工作区改动。
未将这些已有改动纳入 Classic Mixer 提交。manifest.json 记录实际源码和文件哈希，构建不是干净工作区快照。
audio.dll、node.dll、patchbay_core.dll、Qt 和 VC 运行库继续使用现有兼容安装；测试替身及测试程序不在包内。

节点类别 Classic Mixer，分别选择 Classic Mixer 8 / 16 / 24，创建后布局固定且没有选择弹窗。
三个节点位于 nodes/classic_mixer_8、classic_mixer_16、classic_mixer_24，分别携带 locate 资源。
旧 nodes/classic_mixer/mod_classic_mixer.dll 可备份后移出 nodes；新宿主会隐藏旧条目，按旧工程 layoutId 自动加载新 DLL。
打开旧工程不会自动覆写文件；用户保存工程后记录新 DLL 路径。侧栏仅节点名，独立窗口修改通道及预设。
预设随主工程保存，修改后需保存主工程；通道改名自动保留/恢复连接。
已执行离线 DSP、真实 DLL、实际宿主工程恢复和 100% Windows 实窗自动验证。
真实 JACK server 连线/重连、人工审校、人工安装和硬件音频验收未执行；离线替身通过不代表真实 JACK 集成通过。

manifest.json 列出每个运行文件的 SHA-256；ZIP 的 SHA-256 另存于同目录 .sha256 文件。
"""
    with zipfile.ZipFile(
        output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
    ) as archive:
        for name, content in payload.items():
            archive.writestr(name, content)
        archive.writestr(
            "manifest.json", json.dumps(manifest, ensure_ascii=False, indent=2)
        )
        archive.writestr("替换说明.md", readme)

    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None:
            raise ValueError("ZIP CRC verification failed")
        expected_names = set(files) | {"manifest.json", "替换说明.md"}
        if set(archive.namelist()) != expected_names:
            raise ValueError("Unexpected overlay contents")
        for record in manifest["files"]:
            if sha256(archive.read(record["path"])) != record["sha256"]:
                raise ValueError(f"Hash verification failed: {record['path']}")
    digest = sha256(output.read_bytes())
    output.with_suffix(".zip.sha256").write_text(
        f"{digest}  {output.name}\n", encoding="ascii"
    )
    print(f"PASS overlay: {len(files)} runtime files, CRC and SHA-256 verified")
    print(output)
    print(f"Bytes: {output.stat().st_size}; SHA-256: {digest}")


if __name__ == "__main__":
    main()
