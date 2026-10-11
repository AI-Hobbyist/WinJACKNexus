// Node-scoped defaults inherit the shared theme. SPDX-License-Identifier: AGPL-3.0-only
#pragma once
inline constexpr const char midiToolsThemeDescriptor[] = R"theme(
{
  "descriptorVersion": 1,
  "themeApiVersion": 1,
  "moduleId": "midi-tools-sf2-loader",
  "namespace": "--wjn-theme-module-midi-tools-sf2-loader-",
  "displayName": "sf2 loader",
  "tokens": [
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-surface-window",
      "type": "color",
      "default": "var(--wjn-theme-global-surface-window)",
      "inherits": [
        "--wjn-theme-global-surface-window"
      ],
      "description": "module-midi-tools-sf2-loader / surface-window"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-surface-panel",
      "type": "color",
      "default": "var(--wjn-theme-global-surface-panel)",
      "inherits": [
        "--wjn-theme-global-surface-panel"
      ],
      "description": "module-midi-tools-sf2-loader / surface-panel"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-surface-raised",
      "type": "color",
      "default": "var(--wjn-theme-global-surface-raised)",
      "inherits": [
        "--wjn-theme-global-surface-raised"
      ],
      "description": "module-midi-tools-sf2-loader / surface-raised"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-text-primary",
      "type": "color",
      "default": "var(--wjn-theme-global-text-primary)",
      "inherits": [
        "--wjn-theme-global-text-primary"
      ],
      "description": "module-midi-tools-sf2-loader / text-primary"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-text-muted",
      "type": "color",
      "default": "var(--wjn-theme-global-text-muted)",
      "inherits": [
        "--wjn-theme-global-text-muted"
      ],
      "description": "module-midi-tools-sf2-loader / text-muted"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-accent-primary",
      "type": "color",
      "default": "var(--wjn-theme-global-accent-primary)",
      "inherits": [
        "--wjn-theme-global-accent-primary"
      ],
      "description": "module-midi-tools-sf2-loader / accent-primary"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-accent-secondary",
      "type": "color",
      "default": "var(--wjn-theme-global-accent-secondary)",
      "inherits": [
        "--wjn-theme-global-accent-secondary"
      ],
      "description": "module-midi-tools-sf2-loader / accent-secondary"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-border-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-border-normal)",
      "inherits": [
        "--wjn-theme-global-border-normal"
      ],
      "description": "module-midi-tools-sf2-loader / border-normal"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-control-button-surface-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-control-button-surface-normal)",
      "inherits": [
        "--wjn-theme-global-control-button-surface-normal"
      ],
      "description": "module-midi-tools-sf2-loader / control-button-surface-normal"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-control-button-surface-selected",
      "type": "color",
      "default": "var(--wjn-theme-global-control-button-surface-selected)",
      "inherits": [
        "--wjn-theme-global-control-button-surface-selected"
      ],
      "description": "module-midi-tools-sf2-loader / control-button-surface-selected"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-control-button-border-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-control-button-border-normal)",
      "inherits": [
        "--wjn-theme-global-control-button-border-normal"
      ],
      "description": "module-midi-tools-sf2-loader / control-button-border-normal"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-control-button-border-selected",
      "type": "color",
      "default": "var(--wjn-theme-global-control-button-border-selected)",
      "inherits": [
        "--wjn-theme-global-control-button-border-selected"
      ],
      "description": "module-midi-tools-sf2-loader / control-button-border-selected"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-control-button-text-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-control-button-text-normal)",
      "inherits": [
        "--wjn-theme-global-control-button-text-normal"
      ],
      "description": "module-midi-tools-sf2-loader / control-button-text-normal"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-control-button-text-selected",
      "type": "color",
      "default": "var(--wjn-theme-global-control-button-text-selected)",
      "inherits": [
        "--wjn-theme-global-control-button-text-selected"
      ],
      "description": "module-midi-tools-sf2-loader / control-button-text-selected"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-control-button-surface-hover",
      "type": "color",
      "default": "var(--wjn-theme-global-control-button-surface-hover)",
      "inherits": [
        "--wjn-theme-global-control-button-surface-hover"
      ],
      "description": "module-midi-tools-sf2-loader / control-button-surface-hover"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-control-button-text-hover",
      "type": "color",
      "default": "var(--wjn-theme-global-control-button-text-hover)",
      "inherits": [
        "--wjn-theme-global-control-button-text-hover"
      ],
      "description": "module-midi-tools-sf2-loader / control-button-text-hover"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-control-input-surface-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-control-input-surface-normal)",
      "inherits": [
        "--wjn-theme-global-control-input-surface-normal"
      ],
      "description": "module-midi-tools-sf2-loader / control-input-surface-normal"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-control-input-text-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-control-input-text-normal)",
      "inherits": [
        "--wjn-theme-global-control-input-text-normal"
      ],
      "description": "module-midi-tools-sf2-loader / control-input-text-normal"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-control-input-border-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-control-input-border-normal)",
      "inherits": [
        "--wjn-theme-global-control-input-border-normal"
      ],
      "description": "module-midi-tools-sf2-loader / control-input-border-normal"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-control-input-border-focus",
      "type": "color",
      "default": "var(--wjn-theme-global-control-input-border-focus)",
      "inherits": [
        "--wjn-theme-global-control-input-border-focus"
      ],
      "description": "module-midi-tools-sf2-loader / control-input-border-focus"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-control-select-surface-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-control-select-surface-normal)",
      "inherits": [
        "--wjn-theme-global-control-select-surface-normal"
      ],
      "description": "module-midi-tools-sf2-loader / control-select-surface-normal"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-control-select-text-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-control-select-text-normal)",
      "inherits": [
        "--wjn-theme-global-control-select-text-normal"
      ],
      "description": "module-midi-tools-sf2-loader / control-select-text-normal"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-control-select-border-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-control-select-border-normal)",
      "inherits": [
        "--wjn-theme-global-control-select-border-normal"
      ],
      "description": "module-midi-tools-sf2-loader / control-select-border-normal"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-control-list-surface-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-control-list-surface-normal)",
      "inherits": [
        "--wjn-theme-global-control-list-surface-normal"
      ],
      "description": "module-midi-tools-sf2-loader / control-list-surface-normal"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-control-list-text-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-control-list-text-normal)",
      "inherits": [
        "--wjn-theme-global-control-list-text-normal"
      ],
      "description": "module-midi-tools-sf2-loader / control-list-text-normal"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-control-list-border-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-control-list-border-normal)",
      "inherits": [
        "--wjn-theme-global-control-list-border-normal"
      ],
      "description": "module-midi-tools-sf2-loader / control-list-border-normal"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-control-list-surface-selected",
      "type": "color",
      "default": "var(--wjn-theme-global-control-list-surface-selected)",
      "inherits": [
        "--wjn-theme-global-control-list-surface-selected"
      ],
      "description": "module-midi-tools-sf2-loader / control-list-surface-selected"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-control-list-text-selected",
      "type": "color",
      "default": "var(--wjn-theme-global-control-list-text-selected)",
      "inherits": [
        "--wjn-theme-global-control-list-text-selected"
      ],
      "description": "module-midi-tools-sf2-loader / control-list-text-selected"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-control-knob-indicator-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-control-knob-indicator-normal)",
      "inherits": [
        "--wjn-theme-global-control-knob-indicator-normal"
      ],
      "description": "module-midi-tools-sf2-loader / control-knob-indicator-normal"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-meter-level-zone-low-active",
      "type": "color",
      "default": "var(--wjn-theme-global-meter-level-zone-low-active)",
      "inherits": [
        "--wjn-theme-global-meter-level-zone-low-active"
      ],
      "description": "module-midi-tools-sf2-loader / meter-level-zone-low-active"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-meter-level-zone-low-inactive",
      "type": "color",
      "default": "var(--wjn-theme-global-meter-level-zone-low-inactive)",
      "inherits": [
        "--wjn-theme-global-meter-level-zone-low-inactive"
      ],
      "description": "module-midi-tools-sf2-loader / meter-level-zone-low-inactive"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-meter-level-zone-mid-active",
      "type": "color",
      "default": "var(--wjn-theme-global-meter-level-zone-mid-active)",
      "inherits": [
        "--wjn-theme-global-meter-level-zone-mid-active"
      ],
      "description": "module-midi-tools-sf2-loader / meter-level-zone-mid-active"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-meter-level-zone-mid-inactive",
      "type": "color",
      "default": "var(--wjn-theme-global-meter-level-zone-mid-inactive)",
      "inherits": [
        "--wjn-theme-global-meter-level-zone-mid-inactive"
      ],
      "description": "module-midi-tools-sf2-loader / meter-level-zone-mid-inactive"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-meter-level-zone-high-active",
      "type": "color",
      "default": "var(--wjn-theme-global-meter-level-zone-high-active)",
      "inherits": [
        "--wjn-theme-global-meter-level-zone-high-active"
      ],
      "description": "module-midi-tools-sf2-loader / meter-level-zone-high-active"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-meter-level-zone-high-inactive",
      "type": "color",
      "default": "var(--wjn-theme-global-meter-level-zone-high-inactive)",
      "inherits": [
        "--wjn-theme-global-meter-level-zone-high-inactive"
      ],
      "description": "module-midi-tools-sf2-loader / meter-level-zone-high-inactive"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-meter-level-border",
      "type": "color",
      "default": "var(--wjn-theme-global-meter-level-border)",
      "inherits": [
        "--wjn-theme-global-meter-level-border"
      ],
      "description": "module-midi-tools-sf2-loader / meter-level-border"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-meter-level-peak",
      "type": "color",
      "default": "var(--wjn-theme-global-meter-level-peak)",
      "inherits": [
        "--wjn-theme-global-meter-level-peak"
      ],
      "description": "module-midi-tools-sf2-loader / meter-level-peak"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-meter-level-label",
      "type": "color",
      "default": "var(--wjn-theme-global-meter-level-label)",
      "inherits": [
        "--wjn-theme-global-meter-level-label"
      ],
      "description": "module-midi-tools-sf2-loader / meter-level-label"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-meter-level-tick",
      "type": "color",
      "default": "var(--wjn-theme-global-meter-level-tick)",
      "inherits": [
        "--wjn-theme-global-meter-level-tick"
      ],
      "description": "module-midi-tools-sf2-loader / meter-level-tick"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-control-knob-background-base",
      "type": "color",
      "default": "var(--wjn-theme-global-control-knob-background-base)",
      "inherits": [
        "--wjn-theme-global-control-knob-background-base"
      ],
      "description": "module-midi-tools-sf2-loader / control-knob-background-base"
    },
    {
      "name": "--wjn-theme-module-midi-tools-sf2-loader-control-knob-track",
      "type": "color",
      "default": "var(--wjn-theme-global-control-knob-track)",
      "inherits": [
        "--wjn-theme-global-control-knob-track"
      ],
      "description": "module-midi-tools-sf2-loader / control-knob-track"
    }
  ]
}
)theme";
