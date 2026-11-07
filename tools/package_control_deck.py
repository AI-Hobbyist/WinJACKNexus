"""Package the verified Control Deck Release overlay and exact node catalogs."""

from package_broadcast import ROOT, RUNTIME, make_archive


def main() -> None:
    files = {
        name: RUNTIME / name
        for name in ("ui.dll", "control_deck.dll", "mod_patchbay.dll")
    }
    node_root = RUNTIME / "nodes/control_deck"
    manifests = sorted((ROOT / "src/control_deck").glob("*/locate/manifest.json"))
    if len(manifests) != 38:
        raise ValueError(f"Expected 38 node catalogs, found {len(manifests)}")
    for manifest in manifests:
        kind = manifest.parent.parent.name
        filename = f"wjn_signal_switch_{kind}.dll"
        candidates = [
            path
            for path in node_root.rglob(filename)
            if len(path.relative_to(node_root).parts) >= 3
        ]
        if len(candidates) != 1:
            raise ValueError(f"Expected one functional runtime path for {kind}")
        module = candidates[0]
        files[module.relative_to(RUNTIME).as_posix()] = module
        for language in ("manifest", "en-US", "zh-CN", "ja-JP", "ko-KR"):
            source = manifest.parent / f"{language}.json"
            deployed = module.parent / "locate" / source.name
            if source.read_bytes() != deployed.read_bytes():
                raise ValueError(f"Runtime catalog differs from source: {deployed}")
            files[deployed.relative_to(RUNTIME).as_posix()] = deployed
    for name in ("规范.md", "组件开发指南.md", "增量替换说明.md"):
        files[f"ControlDeck/{name}"] = ROOT / "Docs/ControlDeck" / name
    files["licenses/WinJACKNexus-AGPL-3.0.txt"] = ROOT / "LICENSE"
    make_archive(
        "Control-Deck-Overlay-20261107.zip",
        "incremental Control Deck peak, analysis and loudness overlay",
        files,
    )


if __name__ == "__main__":
    main()
