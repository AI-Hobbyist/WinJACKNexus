# Theme template agent instructions

- Read `README.md` and `Theme-Customization.md` before editing this Dev theme template.
- Edit `meta.json`, global `style.css`, the relevant module CSS under `host/`, `node/`, or `native_fx/`, and supported files under `assets/` only for the requested theme. Keep each CSS file to one `:root` block, its corresponding module tokens, the token names, and declared namespaces.
- Keep inherited background image tokens undeclared unless the user requests a module-specific image or explicit `none`.
- Use `python build_theme.py` to create the `.wjnskin`. Inspect the ZIP entries and import it through the theme package tests before reporting completion.
- Save any actual-window verification screenshots under `tests/preview/<stage>/`. Add `-modified` before the extension when token values were changed. Mark visual judgment as `人工验收跳过/未人工验证` unless a person reviewed it.
