/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/UI/LocalizedText.hpp>

namespace WinJACKNexus::UI::ThemeMessages {
// Adapt only display copies, following the existing MixerErrors boundary.
// ThemeCore diagnostics, parser decisions, public tuples and logs stay canonical.
inline QString canonical(const char* english) { return QString::fromUtf8(english); }
struct Rule { const char* key; const char* original; const char* english; };
inline const Rule rules[] {
    { "error.color", "Invalid color value format", "Invalid color value format" },
    { "error.directory", "Cannot create theme directory", "Cannot create theme directory" },
    { "error.imageLimit", "Background image exceeds the size limit or cannot be read", "Background image exceeds the size limit or cannot be read" },
    { "error.imageRead", "Cannot read background image", "Cannot read background image" },
    { "error.fontLimit", "Font file exceeds the size limit or cannot be read", "Font file exceeds the size limit or cannot be read" },
    { "error.fontInvalid", "Invalid font file", "Invalid font file" },
    { "error.position", "Position must be a percentage or px value", "Position must be a percentage or px value" },
    { "error.chooseImage", "Choose a custom image first", "Choose a custom image first" },
    { "core.cssUtf8", "style.css is not UTF-8", "style.css is not UTF-8" },
    { "core.cssSize", "style.css exceeds 1 MiB", "style.css exceeds 1 MiB" },
    { "core.commentOpen", "unclosed CSS comment at line %1", "unclosed CSS comment at line {detail}" },
    { "core.commentEnd", "stray CSS comment end at line %1", "stray CSS comment end at line {detail}" },
    { "core.rootOnly", "CSS requires :root blocks only", "CSS requires :root blocks only" },
    { "core.rootBrace", "CSS root opening brace missing", "CSS root opening brace missing" },
    { "core.parentheses", "unbalanced CSS parentheses", "unbalanced CSS parentheses" },
    { "core.nestedBlock", "nested CSS block is forbidden", "nested CSS block is forbidden" },
    { "core.rootOpen", "unclosed CSS root or parentheses", "unclosed CSS root or parentheses" },
    { "core.declarationColon", "CSS declaration has no colon at line %1", "CSS declaration has no colon at line {detail}" },
    { "core.declarationInvalid", "invalid CSS declaration at line %1", "invalid CSS declaration at line {detail}" },
    { "core.unknownToken", "Unknown token retained in package but ignored for drawing", "Unknown token retained in package but ignored for drawing" },
    { "core.namespaceLine", "token outside declared namespace at line %1", "token outside declared namespace at line {detail}" },
    { "core.duplicateToken", "Later declaration overrides earlier declaration", "Later declaration overrides earlier declaration" },
    { "core.rootRequired", "CSS requires at least one :root block", "CSS requires at least one :root block" },
    { "core.referenceCycle", "Circular token reference", "Circular token reference" },
    { "core.referenceUnresolved", "Unresolved variable reference", "Unresolved variable reference" },
    { "core.valueType", "Value does not match registered type %1", "Value does not match registered type {detail}" },
    { "core.resourceAbsent", "Resource is absent for the resolved token: %1", "Resource is absent for the resolved token: {detail}" },
    { "core.registryNull", "null registry result", "null registry result" },
    { "core.registryVersion", "invalid theme registry version", "invalid theme registry version" },
    { "core.registryCount", "invalid registry token count", "invalid registry token count" },
    { "core.registryObject", "registry token is not an object", "registry token is not an object" },
    { "core.registryToken", "invalid or duplicate registry token: %1", "invalid or duplicate registry token: {detail}" },
    { "core.registryParents", "registry inherits must be an array: %1", "registry inherits must be an array: {detail}" },
    { "core.registryDuplicateParent", "invalid duplicate registry parent: %1", "invalid duplicate registry parent: {detail}" },
    { "core.imageInheritance", "undefined image token requires an inheritance source", "undefined image token requires an inheritance source" },
    { "core.registryInheritance", "registry inheritance is missing, mismatched, or cyclic: %1", "registry inheritance is missing, mismatched, or cyclic: {detail}" },
    { "core.registryUnavailable", "built-in theme registry is unavailable", "built-in theme registry is unavailable" },
    { "core.extendedRegistryNull", "null extended registry result", "null extended registry result" },
    { "core.moduleTokenConflict", "module token conflicts with registered token: %1", "module token conflicts with registered token: {detail}" },
    { "core.registryEmpty", "theme registry is empty", "theme registry is empty" },
    { "core.defaultUnavailable", "required default theme package is unavailable", "required default theme package is unavailable" },
    { "core.fullRequired", "expected a valid full theme package", "expected a valid full theme package" },
    { "core.fragmentRequired", "expected a valid fragment package", "expected a valid fragment package" },
    { "core.overrideToken", "unknown override token", "unknown override token" },
    { "core.previewActive", "preview already active", "preview already active" },
    { "core.fragmentNamespace", "invalid fragment namespace", "invalid fragment namespace" },
    { "core.overrideExportState", "theme override export has no output or active state", "theme override export has no output or active state" },
    { "core.overrideResource", "override resource is missing: %1", "override resource is missing: {detail}" },
    { "core.overrideFragment", "expected a user override fragment", "expected a user override fragment" },
    { "core.exportState", "theme export has no active state or output", "theme export has no active state or output" },
    { "core.sourceCss", "source theme CSS is invalid", "source theme CSS is invalid" },
    { "core.exportResource", "export resource is missing: %1", "export resource is missing: {detail}" },
    { "core.fragmentEmpty", "selected fragment namespace has no declarations", "selected fragment namespace has no declarations" },
    { "core.exportScope", "export declaration has no module scope: %1", "export declaration has no module scope: {detail}" },
};
inline QString display(const QString& source) {
    if (source.size() > 32768) return source;
    for (const auto& rule : rules) {
        const auto original = QString::fromUtf8(rule.original);
        const auto placeholder = original.indexOf(QStringLiteral("%1"));
        if (placeholder < 0) {
            if (source == original) return localizedText("theme", rule.key, rule.english);
            continue;
        }
        const auto prefix = original.left(placeholder);
        const auto suffix = original.mid(placeholder + 2);
        if (source.size() < prefix.size() + suffix.size()
            || !source.startsWith(prefix) || !source.endsWith(suffix)) continue;
        const auto detail = source.mid(prefix.size(), source.size() - prefix.size() - suffix.size());
        // Named argument formatting does not reinterpret token/path contents.
        return localizedText("theme", rule.key, rule.english,
                             {{QStringLiteral("detail"), detail}});
    }
    return source; // Unknown SDK/OS/user content is not relabeled as a project error.
}
}
