// Node-scoped defaults inherit the shared theme. SPDX-License-Identifier: AGPL-3.0-only
#pragma once
inline constexpr const char midiToolsThemeDescriptor[] = R"theme(
{
  "descriptorVersion": 1,
  "themeApiVersion": 1,
  "moduleId": "midi-tools-midi-spilit",
  "namespace": "--wjn-theme-module-midi-tools-midi-spilit-",
  "displayName": "midi spilit",
  "tokens": [
    {
      "name": "--wjn-theme-module-midi-tools-midi-spilit-surface-window",
      "type": "color",
      "default": "var(--wjn-theme-global-surface-window)",
      "inherits": [
        "--wjn-theme-global-surface-window"
      ],
      "description": "module-midi-tools-midi-spilit / surface-window"
    },
    {
      "name": "--wjn-theme-module-midi-tools-midi-spilit-surface-panel",
      "type": "color",
      "default": "var(--wjn-theme-global-surface-panel)",
      "inherits": [
        "--wjn-theme-global-surface-panel"
      ],
      "description": "module-midi-tools-midi-spilit / surface-panel"
    },
    {
      "name": "--wjn-theme-module-midi-tools-midi-spilit-surface-raised",
      "type": "color",
      "default": "var(--wjn-theme-global-surface-raised)",
      "inherits": [
        "--wjn-theme-global-surface-raised"
      ],
      "description": "module-midi-tools-midi-spilit / surface-raised"
    },
    {
      "name": "--wjn-theme-module-midi-tools-midi-spilit-text-primary",
      "type": "color",
      "default": "var(--wjn-theme-global-text-primary)",
      "inherits": [
        "--wjn-theme-global-text-primary"
      ],
      "description": "module-midi-tools-midi-spilit / text-primary"
    },
    {
      "name": "--wjn-theme-module-midi-tools-midi-spilit-text-muted",
      "type": "color",
      "default": "var(--wjn-theme-global-text-muted)",
      "inherits": [
        "--wjn-theme-global-text-muted"
      ],
      "description": "module-midi-tools-midi-spilit / text-muted"
    },
    {
      "name": "--wjn-theme-module-midi-tools-midi-spilit-accent-primary",
      "type": "color",
      "default": "var(--wjn-theme-global-accent-primary)",
      "inherits": [
        "--wjn-theme-global-accent-primary"
      ],
      "description": "module-midi-tools-midi-spilit / accent-primary"
    },
    {
      "name": "--wjn-theme-module-midi-tools-midi-spilit-accent-secondary",
      "type": "color",
      "default": "var(--wjn-theme-global-accent-secondary)",
      "inherits": [
        "--wjn-theme-global-accent-secondary"
      ],
      "description": "module-midi-tools-midi-spilit / accent-secondary"
    },
    {
      "name": "--wjn-theme-module-midi-tools-midi-spilit-border-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-border-normal)",
      "inherits": [
        "--wjn-theme-global-border-normal"
      ],
      "description": "module-midi-tools-midi-spilit / border-normal"
    },
    {
      "name": "--wjn-theme-module-midi-tools-midi-spilit-control-button-surface-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-control-button-surface-normal)",
      "inherits": [
        "--wjn-theme-global-control-button-surface-normal"
      ],
      "description": "module-midi-tools-midi-spilit / control-button-surface-normal"
    },
    {
      "name": "--wjn-theme-module-midi-tools-midi-spilit-control-button-surface-selected",
      "type": "color",
      "default": "var(--wjn-theme-global-control-button-surface-selected)",
      "inherits": [
        "--wjn-theme-global-control-button-surface-selected"
      ],
      "description": "module-midi-tools-midi-spilit / control-button-surface-selected"
    },
    {
      "name": "--wjn-theme-module-midi-tools-midi-spilit-control-button-border-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-control-button-border-normal)",
      "inherits": [
        "--wjn-theme-global-control-button-border-normal"
      ],
      "description": "module-midi-tools-midi-spilit / control-button-border-normal"
    },
    {
      "name": "--wjn-theme-module-midi-tools-midi-spilit-control-button-border-selected",
      "type": "color",
      "default": "var(--wjn-theme-global-control-button-border-selected)",
      "inherits": [
        "--wjn-theme-global-control-button-border-selected"
      ],
      "description": "module-midi-tools-midi-spilit / control-button-border-selected"
    },
    {
      "name": "--wjn-theme-module-midi-tools-midi-spilit-control-button-text-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-control-button-text-normal)",
      "inherits": [
        "--wjn-theme-global-control-button-text-normal"
      ],
      "description": "module-midi-tools-midi-spilit / control-button-text-normal"
    },
    {
      "name": "--wjn-theme-module-midi-tools-midi-spilit-control-button-text-selected",
      "type": "color",
      "default": "var(--wjn-theme-global-control-button-text-selected)",
      "inherits": [
        "--wjn-theme-global-control-button-text-selected"
      ],
      "description": "module-midi-tools-midi-spilit / control-button-text-selected"
    },
    {
      "name": "--wjn-theme-module-midi-tools-midi-spilit-control-button-surface-hover",
      "type": "color",
      "default": "var(--wjn-theme-global-control-button-surface-hover)",
      "inherits": [
        "--wjn-theme-global-control-button-surface-hover"
      ],
      "description": "module-midi-tools-midi-spilit / control-button-surface-hover"
    },
    {
      "name": "--wjn-theme-module-midi-tools-midi-spilit-control-button-text-hover",
      "type": "color",
      "default": "var(--wjn-theme-global-control-button-text-hover)",
      "inherits": [
        "--wjn-theme-global-control-button-text-hover"
      ],
      "description": "module-midi-tools-midi-spilit / control-button-text-hover"
    },
    {
      "name": "--wjn-theme-module-midi-tools-midi-spilit-control-input-surface-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-control-input-surface-normal)",
      "inherits": [
        "--wjn-theme-global-control-input-surface-normal"
      ],
      "description": "module-midi-tools-midi-spilit / control-input-surface-normal"
    },
    {
      "name": "--wjn-theme-module-midi-tools-midi-spilit-control-input-text-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-control-input-text-normal)",
      "inherits": [
        "--wjn-theme-global-control-input-text-normal"
      ],
      "description": "module-midi-tools-midi-spilit / control-input-text-normal"
    },
    {
      "name": "--wjn-theme-module-midi-tools-midi-spilit-control-input-border-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-control-input-border-normal)",
      "inherits": [
        "--wjn-theme-global-control-input-border-normal"
      ],
      "description": "module-midi-tools-midi-spilit / control-input-border-normal"
    },
    {
      "name": "--wjn-theme-module-midi-tools-midi-spilit-control-input-border-focus",
      "type": "color",
      "default": "var(--wjn-theme-global-control-input-border-focus)",
      "inherits": [
        "--wjn-theme-global-control-input-border-focus"
      ],
      "description": "module-midi-tools-midi-spilit / control-input-border-focus"
    }
  ]
}
)theme";
