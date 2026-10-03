# Meter source review — 2026-10-04

Reviewed all 49 Meter candidates against current producers and consumers. This
source review changes inventories only, retains S4 runtime evidence and launches
no new module run or window. The two role-label gaps below remain explicit.

| Producer content | Current display/storage path | Decision |
| --- | --- | --- |
| Known preset custom displayName | MeterCard populate/refresh → Localized::presetName → preset.custom; selection uses original preset ID | Keep canonical model metadata; actual display translation already exists. |
| Custom preset invalid/directory failures | MeterCard save handler recognizes canonical invalid message/directory prefix and displays preset.invalid/preset.directoryFailure | Preserve API error and original directory; display copy translated. |
| Live/raw layout labels and preview layout labels | MeterCard and MeterHistoryWindow → Localized::layoutName using channel count/custom flag; Live provider also exports original layout JSON | Preserve raw snapshot/export metadata; layout.* display already exists. |
| CSV worker/provider/preview rotationStatus | Stable MeterCsvStatusKind, dropped count and raw detail → MeterHistoryWindow::csvDisplayText → csv.* | Preserve compatibility strings and worker data. The project's producers set specific kinds; the unspecified fallback remains for external caller text. |
| Unnamed CSV directory fallback | sanitizeDirectoryName → log directory/file path | Keep file identity; changing language must not rename existing paths. |
| Preview client names | Mock snapshots simulate original client/user names → MeterCard clientName text and nameRevision | Preserve fixture/project content using the same rule as actual client names. |
| Eleven scenarioDisplayName labels | Static helper declaration/definition only; no src/tools/Develop application consumer | Preserve unused preview helper; review again if it gains a UI consumer. |
| Two live 未分配 role labels | speakerRoleLabel → JSON role field / MeterChannelView.speakerRole → MeterSettingsWidget roleSelector addItem and currentText dispatch | **Confirmed UI gaps.** Implement translated itemText plus canonical itemData; never write translated role into commands or JSON. |

S6-meter-source-audit.json records every original file/line/context, its explicit
decision and producer/consumer SHA-256. verify_s6_meter_source_audit.py checks
all 49 decisions against current classification, enforces 47 preserved and two
open rows, and checks that the scenario helper still has no application caller.
Classification uses exact source/context matches rather than excluding a whole
model file. This does not prove every runtime branch or approve translations.

The remaining global navigation inventory is 59 occurrences. S6 and human review
remain incomplete; no extra Meter test run will be launched under the user's
single-run policy. The role display correction and any other confirmed UI gaps
must be completed before the necessary final Release build.
