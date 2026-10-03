# Theme template agent instructions

- Read `README.md`, `Theme-Customization.md`, and the local `Theme-v1-Registry.json` before editing this Dev theme template. The registry defines each token's type, module scope, default, and inheritance.
- Aim to cover every installed host, Node, and Native Fx module when delivering a complete theme. Review each registered namespace and document any module intentionally left on inherited defaults.
- Edit `meta.json`, global `style.css`, the relevant module CSS under `host/`, `node/`, or `native_fx/`, and supported files under `assets/` only for the requested theme. Keep each CSS file to one `:root` block, its corresponding module tokens, the token names, and declared namespaces.
- Keep inherited background image tokens undeclared unless the user requests a module-specific image or explicit `none`.
- Keep text legible against every normal, hover, selected, disabled, and translucent surface. Text and its background must have clear contrast; do not choose near-matching colors. Review text after backgrounds or opacity change.
- Use `python build_theme.py` to create the `.wjnskin`; theme appearance is accepted by a person. Do not require automated tests for theme editing.
- Save any actual-window review screenshots under `tests/preview/<stage>/`. Add `-modified` before the extension when token values were changed. Until a person reviews them, record `人工验收跳过/未人工验证`, never "passed".
