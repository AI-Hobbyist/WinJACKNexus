"""Package the verified S1-S6 runtime overlay from the existing Release tree."""

import hashlib
import json
from pathlib import Path
import subprocess
import zipfile


ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ROOT / "build/release/bin"
OUTPUT = ROOT / "build/WinJACKNexus-PrePublic-S1-S6-Release-overlay.zip"
LANGUAGES = ("en-US", "zh-CN", "ja-JP", "ko-KR")


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def main():
    files = [
        "WinJACKNexus.exe", "common.dll", "node.dll", "ui.dll", "mod_patchbay.dll",
        "mod_meterbrigde.dll", "VSTProcess32.exe", "VSTProcess64.exe",
        "nodes/mixer/mod_digital_mixer.dll", "nodes/vst/mod_vst_host.dll",
    ]
    for name in ("wasapi_in", "wasapi_out", "system_midi_in", "system_midi_out"):
        files.append(f"nodes/adapter/{name}/mod_{name}.dll")
    for name in ("meter_mono", "meter_stereo", "meter_5_1", "meter_7_1", "meter_custom"):
        files.append(f"nodes/meter/{name}/mod_{name}.dll")
    for name in ("gate", "graphic_eq", "deesser", "compressor", "limiter", "delay",
                 "parametric_eq", "reverb"):
        files.append(f"nodes/native_fx/{name}/mod_{name}.dll")
    for name in ("midi_spilit", "midi_matrix", "sf2_loader"):
        for folder in ("midi_tools", "nodes/midi_tools"):
            files.append(f"{folder}/{name}/mod_{name}.dll")
    # These catalogs changed with the sidebar capabilities, Split spelling or SF2 lifecycle.
    catalog_roots = ["locate"]
    for folder in ("midi_tools", "nodes/midi_tools"):
        for name in ("midi_spilit", "sf2_loader"):
            catalog_roots.append(f"{folder}/{name}/locate")
    for folder in catalog_roots:
        files.extend(f"{folder}/{name}.json" for name in (*LANGUAGES, "manifest"))

    payload = {}
    for name in sorted(files):
        payload[name] = (RUNTIME / name).read_bytes()
    for name in ("midi_spilit", "midi_matrix", "sf2_loader"):
        assert payload[f"midi_tools/{name}/mod_{name}.dll"] == payload[
            f"nodes/midi_tools/{name}/mod_{name}.dll"], "stale duplicate MIDI output"
    manifest = {
        "kind": "incremental Release overlay",
        "fullBuild": False,
        "sourceCheckpoint": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "versions": {"Node": 4, "NativeFxBoundary": 2, "VstBlockIpc": 1,
                     "QtPrerequisite": "6.10.3 msvc2022_64 Release"},
        "provenance": "Built from the current workspace; pre-existing uncommitted edits remain outside milestone commits.",
        "prerequisite": "Existing complete installation and its unchanged Qt/JACK/synth dependencies.",
        "files": [{"path": name, "bytes": len(data), "sha256": sha256(data)}
                  for name, data in payload.items()],
    }
    install = """WinJACKNexus S1-S6 Release 增量替换包（非完整安装包）

适用于已有完整运行目录，保留原有 Qt 6.10.3、Windows 平台插件、JACK、
FluidSynth/SFZ 运行依赖、音色库、插件、工程、主题和用户设置。

安装顺序：
1. 用户自行结束应用及其工作进程后，备份原运行目录。打包过程没有停止用户音频。
2. 将本 ZIP 的 runtime/ 内容全部合并覆盖到 WinJACKNexus.exe 所在目录。
3. 同一次操作替换宿主/运行库、所有清单内 Node v4 DLL、NativeFx boundary v2、
   VSTProcess32/64 和对应语言资源。不要启动混用新宿主与旧 ABI 模块的目录。
4. 按 manifest.json 核对文件 SHA-256 后启动；打开旧工程检查诊断并自行保存。
   第三方 Node v3 必须按 v4 重编；不可加载节点的参数和连线记录保留。

SDK 示例已在仓库迁移并验证，本运行包不含 SDK 源码、示例或测试夹具。
旧聚合 mod_adapter.dll/mod_meterbridge_probe.dll 不在当前构建目标，未打包。
没有变更既有 JACK 端口名，MIDI Split 仅修正显示名与翻译。
实窗验证仅 Windows 100%；真实 JACK、硬件、第三方实际插件和安装人工验收
已跳过（用户指令）。本包从当前工作区编译，已有未提交修改不属于阶段提交，
但可能包含在相关已编译二进制内；源版本和各文件哈希见清单。
"""
    manifest_bytes = (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode()
    with zipfile.ZipFile(OUTPUT, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, data in payload.items():
            archive.writestr("runtime/" + name, data)
        archive.writestr("manifest.json", manifest_bytes)
        archive.writestr("INSTALL.txt", install.encode("utf-8-sig"))
    with zipfile.ZipFile(OUTPUT) as archive:
        assert archive.testzip() is None, "ZIP CRC verification"
        assert len(archive.namelist()) == len(payload) + 2
        for record in manifest["files"]:
            assert sha256(archive.read("runtime/" + record["path"])) == record["sha256"]
        assert not any("fixture" in name.lower() or name.lower().endswith(".pdb")
                       or "libjack" in name.lower() for name in archive.namelist())
    digest = sha256(OUTPUT.read_bytes())
    OUTPUT.with_suffix(".zip.sha256").write_text(digest + "  " + OUTPUT.name + "\n")
    (ROOT / "Docs/Node/PrePublic-S6-package-manifest.json").write_bytes(manifest_bytes)
    (ROOT / "Docs/Node/PrePublic-S6-INSTALL.txt").write_text(install, encoding="utf-8")
    print(f"PASS incremental package: {OUTPUT}; {len(payload)} runtime files; CRC and SHA-256 verified")
    print(f"ZIP SHA-256 {digest}")


if __name__ == "__main__":
    main()
