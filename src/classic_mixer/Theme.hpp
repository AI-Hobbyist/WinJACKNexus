// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
inline constexpr const char classicMixerTheme[] = R"theme(
{
  "descriptorVersion": 1,
  "themeApiVersion": 1,
  "moduleId": "winjacknexus-classic-mixer",
  "namespace": "--wjn-theme-module-winjacknexus-classic-mixer-",
  "displayName": "Classic Mixer",
  "tokens": [
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-surface-window",
      "type": "color",
      "default": "var(--wjn-theme-global-surface-window)",
      "inherits": [
        "--wjn-theme-global-surface-window"
      ],
      "description": "module-winjacknexus-classic-mixer / surface-window"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-surface-panel",
      "type": "color",
      "default": "var(--wjn-theme-global-surface-panel)",
      "inherits": [
        "--wjn-theme-global-surface-panel"
      ],
      "description": "module-winjacknexus-classic-mixer / surface-panel"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-surface-raised",
      "type": "color",
      "default": "var(--wjn-theme-global-surface-raised)",
      "inherits": [
        "--wjn-theme-global-surface-raised"
      ],
      "description": "module-winjacknexus-classic-mixer / surface-raised"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-text-primary",
      "type": "color",
      "default": "var(--wjn-theme-global-text-primary)",
      "inherits": [
        "--wjn-theme-global-text-primary"
      ],
      "description": "module-winjacknexus-classic-mixer / text-primary"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-text-muted",
      "type": "color",
      "default": "var(--wjn-theme-global-text-muted)",
      "inherits": [
        "--wjn-theme-global-text-muted"
      ],
      "description": "module-winjacknexus-classic-mixer / text-muted"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-accent-primary",
      "type": "color",
      "default": "var(--wjn-theme-global-accent-primary)",
      "inherits": [
        "--wjn-theme-global-accent-primary"
      ],
      "description": "module-winjacknexus-classic-mixer / accent-primary"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-accent-secondary",
      "type": "color",
      "default": "var(--wjn-theme-global-accent-secondary)",
      "inherits": [
        "--wjn-theme-global-accent-secondary"
      ],
      "description": "module-winjacknexus-classic-mixer / accent-secondary"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-border-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-border-normal)",
      "inherits": [
        "--wjn-theme-global-border-normal"
      ],
      "description": "module-winjacknexus-classic-mixer / border-normal"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-control-button-surface-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-control-button-surface-normal)",
      "inherits": [
        "--wjn-theme-global-control-button-surface-normal"
      ],
      "description": "module-winjacknexus-classic-mixer / control-button-surface-normal"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-control-button-surface-selected",
      "type": "color",
      "default": "var(--wjn-theme-global-control-button-surface-selected)",
      "inherits": [
        "--wjn-theme-global-control-button-surface-selected"
      ],
      "description": "module-winjacknexus-classic-mixer / control-button-surface-selected"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-control-button-border-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-control-button-border-normal)",
      "inherits": [
        "--wjn-theme-global-control-button-border-normal"
      ],
      "description": "module-winjacknexus-classic-mixer / control-button-border-normal"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-control-button-border-selected",
      "type": "color",
      "default": "var(--wjn-theme-global-control-button-border-selected)",
      "inherits": [
        "--wjn-theme-global-control-button-border-selected"
      ],
      "description": "module-winjacknexus-classic-mixer / control-button-border-selected"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-control-button-text-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-control-button-text-normal)",
      "inherits": [
        "--wjn-theme-global-control-button-text-normal"
      ],
      "description": "module-winjacknexus-classic-mixer / control-button-text-normal"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-control-button-text-selected",
      "type": "color",
      "default": "var(--wjn-theme-global-control-button-text-selected)",
      "inherits": [
        "--wjn-theme-global-control-button-text-selected"
      ],
      "description": "module-winjacknexus-classic-mixer / control-button-text-selected"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-control-button-surface-hover",
      "type": "color",
      "default": "var(--wjn-theme-global-control-button-surface-hover)",
      "inherits": [
        "--wjn-theme-global-control-button-surface-hover"
      ],
      "description": "module-winjacknexus-classic-mixer / control-button-surface-hover"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-control-button-text-hover",
      "type": "color",
      "default": "var(--wjn-theme-global-control-button-text-hover)",
      "inherits": [
        "--wjn-theme-global-control-button-text-hover"
      ],
      "description": "module-winjacknexus-classic-mixer / control-button-text-hover"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-control-input-surface-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-control-input-surface-normal)",
      "inherits": [
        "--wjn-theme-global-control-input-surface-normal"
      ],
      "description": "module-winjacknexus-classic-mixer / control-input-surface-normal"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-control-input-text-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-control-input-text-normal)",
      "inherits": [
        "--wjn-theme-global-control-input-text-normal"
      ],
      "description": "module-winjacknexus-classic-mixer / control-input-text-normal"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-control-input-border-normal",
      "type": "color",
      "default": "var(--wjn-theme-global-control-input-border-normal)",
      "inherits": [
        "--wjn-theme-global-control-input-border-normal"
      ],
      "description": "module-winjacknexus-classic-mixer / control-input-border-normal"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-control-input-border-focus",
      "type": "color",
      "default": "var(--wjn-theme-global-control-input-border-focus)",
      "inherits": [
        "--wjn-theme-global-control-input-border-focus"
      ],
      "description": "module-winjacknexus-classic-mixer / control-input-border-focus"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-control-knob-indicator-normal",
      "type": "color",
      "default": "#31c6ff",
      "description": "Classic Mixer / control-knob-indicator-normal",
      "inherits": [
        "--wjn-theme-global-control-knob-indicator-normal"
      ]
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-meter-level-zone-low-active",
      "type": "color",
      "default": "#2bce75",
      "description": "Classic Mixer / meter-level-zone-low-active",
      "inherits": [
        "--wjn-theme-global-meter-level-zone-low-active"
      ]
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-meter-level-zone-low-inactive",
      "type": "color",
      "default": "#163e2e",
      "description": "Classic Mixer / meter-level-zone-low-inactive",
      "inherits": [
        "--wjn-theme-global-meter-level-zone-low-inactive"
      ]
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-meter-level-zone-mid-active",
      "type": "color",
      "default": "#f4cd42",
      "description": "Classic Mixer / meter-level-zone-mid-active",
      "inherits": [
        "--wjn-theme-global-meter-level-zone-mid-active"
      ]
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-meter-level-zone-mid-inactive",
      "type": "color",
      "default": "#4a3f1d",
      "description": "Classic Mixer / meter-level-zone-mid-inactive",
      "inherits": [
        "--wjn-theme-global-meter-level-zone-mid-inactive"
      ]
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-meter-level-zone-high-active",
      "type": "color",
      "default": "#f35656",
      "description": "Classic Mixer / meter-level-zone-high-active",
      "inherits": [
        "--wjn-theme-global-meter-level-zone-high-active"
      ]
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-meter-level-zone-high-inactive",
      "type": "color",
      "default": "#4c242a",
      "description": "Classic Mixer / meter-level-zone-high-inactive",
      "inherits": [
        "--wjn-theme-global-meter-level-zone-high-inactive"
      ]
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-meter-level-border",
      "type": "color",
      "default": "#426181",
      "description": "Classic Mixer / meter-level-border",
      "inherits": [
        "--wjn-theme-global-meter-level-border"
      ]
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-meter-level-peak",
      "type": "color",
      "default": "#f7fbff",
      "description": "Classic Mixer / meter-level-peak",
      "inherits": [
        "--wjn-theme-global-meter-level-peak"
      ]
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-meter-level-label",
      "type": "color",
      "default": "#afc7e5",
      "description": "Classic Mixer / meter-level-label",
      "inherits": [
        "--wjn-theme-global-meter-level-label"
      ]
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-meter-level-tick",
      "type": "color",
      "default": "#526763",
      "description": "Classic Mixer / meter-level-tick",
      "inherits": [
        "--wjn-theme-global-meter-level-tick"
      ]
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-control-knob-background-base",
      "type": "color",
      "default": "#0b1929",
      "description": "Classic Mixer / control-knob-background-base",
      "inherits": [
        "--wjn-theme-global-control-knob-background-base"
      ]
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-control-knob-track",
      "type": "color",
      "default": "#29435d",
      "description": "Classic Mixer / control-knob-track",
      "inherits": [
        "--wjn-theme-global-control-knob-track"
      ]
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-channel-divider",
      "type": "color",
      "default": "#526763",
      "description": "Classic Mixer / channel-divider",
      "inherits": [
        "--wjn-theme-global-border-normal"
      ]
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-fader-input-thumb",
      "type": "color",
      "default": "#a6adb4",
      "description": "Classic Mixer / fader-input-thumb"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-fader-input-track",
      "type": "color",
      "default": "#172025",
      "description": "Classic Mixer / fader-input-track"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-fader-input-grip",
      "type": "color",
      "default": "#11181a",
      "description": "Classic Mixer / fader-input-grip"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-fader-master-thumb",
      "type": "color",
      "default": "#e35252",
      "description": "Classic Mixer / fader-master-thumb"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-fader-master-track",
      "type": "color",
      "default": "#172025",
      "description": "Classic Mixer / fader-master-track"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-fader-master-grip",
      "type": "color",
      "default": "#11181a",
      "description": "Classic Mixer / fader-master-grip"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-fader-monitor-thumb",
      "type": "color",
      "default": "#e4c44c",
      "description": "Classic Mixer / fader-monitor-thumb"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-fader-monitor-track",
      "type": "color",
      "default": "#172025",
      "description": "Classic Mixer / fader-monitor-track"
    },
    {
      "name": "--wjn-theme-module-winjacknexus-classic-mixer-fader-monitor-grip",
      "type": "color",
      "default": "#11181a",
      "description": "Classic Mixer / fader-monitor-grip"
    }
  ]
}
)theme";
