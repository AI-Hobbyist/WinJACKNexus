# Simple Dev theme instructions

- Read `README.md`, `../template/Theme-Customization.md`, and the local `Theme-v1-Registry.json` before editing. Use the registry to check token type, module scope, default, and inheritance.
- Use this sparse template to start quickly, then aim to cover every installed host, Node, and Native Fx module before delivering a complete theme. Review each registered namespace and document any module intentionally left on inherited defaults.
- Keep the `mixer` fader thumb tokens and `sel-main` per-type accent aliases distinct from channel custom colors. Never use a channel's color-picker value as a SEL control token.
- Preserve one `:root` block per CSS file and the declared module namespace.
- Keep text and background colors clearly distinguishable in every state, including translucent overlays; never use near-matching colors. Recheck text contrast after changing a fader or SEL accent.
- Use `python build_theme.py` to package the theme. Theme appearance is accepted by a person; automated tests are not required for theme editing.
- Save actual-window review screenshots under `tests/preview/<stage>/`; changed-token screenshots end in `-modified`. Until a person reviews them, record `人工验收跳过/未人工验证`.
