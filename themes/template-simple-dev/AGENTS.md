# Simple Dev theme instructions

- Read `README.md`, the local `Theme-Customization.md`, and `Theme-v1-Registry.json` before editing. Use the registry to check token type, module scope, default, and inheritance.
- Preserve the complete template structure: global, every host, Node, and Native Fx module CSS, and all declared namespaces. Simple means selected defaults inherit via `var(...)`; do not omit modules or their Token declarations.
- Aim to cover every installed module when delivering a theme. Review each namespace and document values intentionally left inherited.
- Keep the `mixer` fader thumb tokens and `sel-main` per-type accent aliases distinct from channel custom colors. Never use a channel's color-picker value as a SEL control token.
- Preserve one `:root` block per CSS file and the declared module namespace.
- Keep text and background colors clearly distinguishable in every state, including translucent overlays; never use near-matching colors. Recheck text contrast after changing a fader or SEL accent.
- Use `python build_theme.py` to package the theme. Theme appearance is accepted by a person; automated tests are not required for theme editing.
- Save actual-window review screenshots under `tests/preview/<stage>/`; changed-token screenshots end in `-modified`. Until a person reviews them, record `人工验收跳过/未人工验证`.
