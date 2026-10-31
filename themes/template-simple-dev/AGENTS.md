# Simple Dev theme instructions

- Read `README.md`, the local `Theme-Customization.md`, and `Theme-v1-Registry.json` before editing. Use the registry to check token type, module scope, default, and inheritance.
- Preserve the complete template structure: global, every host, Node, and Native Fx module CSS, and all declared namespaces. Simple means selected defaults inherit via `var(...)`; do not omit modules or their Token declarations.
- Aim to cover every installed module when delivering a theme. Review each namespace and document values intentionally left inherited.
- Before changing default fader or SEL colors, compare the resolved Simple Dev tokens with the package actually exported by `wjn_default_theme.dll`. Keep unchanged defaults synchronized by referencing the shared semantic token with `var(...)`; do not duplicate a matching hex value or independently alter one side. Record any intentional difference and the reason in the theme documentation.
- Keep the five type-specific `mixer` fader thumb colors as their source tokens; AUX may inherit the equal-valued global generic thumb. Inherit shared fader tracks and grips from their global generic tokens. The five `sel-main-control-accent-*` tokens must continue to reference the matching fader thumb token, never a channel's color-picker value.
- Keep shared SEL Main/AUX overall surfaces, text, borders, and control colors linked through `var(...)`. Classic PAN's inner circle reads `sel-main` or `sel-aux` `control-knob-panel`; in this Simple Dev template that token must reference the corresponding SEL `control-knob-background-base` inner-bottom token. This PAN alias is an intentional visual difference from the current default DLL and must remain explicit when comparing defaults.
- Preserve one `:root` block per CSS file and the declared module namespace.
- Keep text and background colors clearly distinguishable in every state, including translucent overlays; never use near-matching colors. Recheck text contrast after changing a fader or SEL accent.
- Use `python build_theme.py` to package the theme. Theme appearance is accepted by a person; automated tests are not required for theme editing.
- Save actual-window review screenshots under `tests/preview/<stage>/`; changed-token screenshots end in `-modified`. Until a person reviews them, record `人工验收跳过/未人工验证`.
