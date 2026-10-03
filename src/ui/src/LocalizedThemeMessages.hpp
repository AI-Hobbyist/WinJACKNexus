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
    { "package.metaSize", "meta.json size is invalid", "meta.json size is invalid" },
    { "package.metaUtf8", "meta.json is not UTF-8", "meta.json is not UTF-8" },
    { "package.metaObject", "meta.json is not a JSON object", "meta.json is not a JSON object" },
    { "package.apiVersion", "unsupported theme format/API version", "unsupported theme format/API version" },
    { "package.id", "invalid theme id", "invalid theme id" },
    { "package.version", "invalid theme version", "invalid theme version" },
    { "package.metadata", "theme name/author/description is invalid", "theme name/author/description is invalid" },
    { "package.homepageMissing", "theme homepage is missing", "theme homepage is missing" },
    { "package.homepage", "invalid theme homepage", "invalid theme homepage" },
    { "package.kind", "invalid theme kind", "invalid theme kind" },
    { "package.namespacesMissing", "missing namespaces", "missing namespaces" },
    { "package.namespace", "invalid or duplicate namespace", "invalid or duplicate namespace" },
    { "package.previewPath", "invalid preview path", "invalid preview path" },
    { "package.storedSize", "stored entry size mismatch", "stored entry size mismatch" },
    { "package.zlibInit", "zlib init failed", "zlib init failed" },
    { "package.expandedSize", "expanded entry exceeded declared size", "expanded entry exceeded declared size" },
    { "package.deflate", "deflate stream is truncated or malformed", "deflate stream is truncated or malformed" },
    { "package.crc", "ZIP CRC mismatch: %1", "ZIP CRC mismatch: {detail}" },
    { "package.size", "package size is outside 22 bytes..32 MiB", "package size is outside 22 bytes..32 MiB" },
    { "package.zipEnd", "ZIP end record missing", "ZIP end record missing" },
    { "package.zip64Disks", "multi-disk ZIP64 is unsupported", "multi-disk ZIP64 is unsupported" },
    { "package.zip64End", "invalid ZIP64 end record", "invalid ZIP64 end record" },
    { "package.zipBounds", "unsupported ZIP disk or central directory bounds", "unsupported ZIP disk or central directory bounds" },
    { "package.zipCentral", "invalid ZIP central record", "invalid ZIP central record" },
    { "package.zipCentralTruncated", "truncated ZIP central entry", "truncated ZIP central entry" },
    { "package.entryUtf8", "entry name is not UTF-8", "entry name is not UTF-8" },
    { "package.zipPath", "unsafe, unsupported, or duplicate ZIP path: %1", "unsafe, unsupported, or duplicate ZIP path: {detail}" },
    { "package.zipMethod", "unsupported ZIP encryption or method", "unsupported ZIP encryption or method" },
    { "package.zip64Central", "invalid ZIP64 central entry", "invalid ZIP64 central entry" },
    { "package.zipSymlink", "ZIP symbolic links are not allowed", "ZIP symbolic links are not allowed" },
    { "package.directoryContent", "directory has content", "directory has content" },
    { "package.resourceBudget", "ZIP resource budget exceeded: %1", "ZIP resource budget exceeded: {detail}" },
    { "package.rootLayout", "ZIP root layout is incomplete", "ZIP root layout is incomplete" },
    { "package.localHeader", "invalid ZIP local header", "invalid ZIP local header" },
    { "package.flags", "ZIP local/central flags differ", "ZIP local/central flags differ" },
    { "package.localBounds", "ZIP local name or data bounds differ", "ZIP local name or data bounds differ" },
    { "package.zip64Local", "invalid ZIP64 local entry", "invalid ZIP64 local entry" },
    { "package.localSizes", "ZIP local/central sizes differ", "ZIP local/central sizes differ" },
    { "package.dataDescriptor", "invalid ZIP data descriptor", "invalid ZIP data descriptor" },
    { "package.centralOverlap", "ZIP entry overlaps central directory", "ZIP entry overlaps central directory" },
    { "package.expandedBudget", "expanded package budget exceeded", "expanded package budget exceeded" },
    { "package.entriesOverlap", "ZIP entries overlap", "ZIP entries overlap" },
    { "package.entryCount", "too many ZIP entries", "too many ZIP entries" },
    { "package.outputBudget", "ZIP output exceeds size budget", "ZIP output exceeds size budget" },
    { "package.outputSize", "ZIP output exceeds 32 MiB", "ZIP output exceeds 32 MiB" },
    { "package.nullResult", "null theme result", "null theme result" },
    { "package.open", "cannot open theme package: %1", "cannot open theme package: {detail}" },
    { "package.fileSize", "theme package exceeds 32 MiB", "theme package exceeds 32 MiB" },
    { "package.directory", "theme directory is missing or linked", "theme directory is missing or linked" },
    { "package.devEntry", "unsupported Dev theme entry: %1", "unsupported Dev theme entry: {detail}" },
    { "package.devCount", "duplicate or excessive Dev theme entries", "duplicate or excessive Dev theme entries" },
    { "package.devEntrySize", "Dev theme entry exceeds size budget: %1", "Dev theme entry exceeds size budget: {detail}" },
    { "package.devRead", "cannot read Dev theme entry: %1", "cannot read Dev theme entry: {detail}" },
    { "package.devChanged", "Dev theme entry changed while reading: %1", "Dev theme entry changed while reading: {detail}" },
    { "package.devBudget", "Dev theme exceeds package budget", "Dev theme exceeds package budget" },
    { "package.defaultDll", "required default theme DLL is unavailable: %1", "required default theme DLL is unavailable: {detail}" },
    { "package.defaultApi", "default theme DLL API is incompatible", "default theme DLL API is incompatible" },
    { "package.defaultData", "default theme DLL has no valid package data", "default theme DLL has no valid package data" },
    { "package.cssUtf8", "style.css is empty or not UTF-8", "style.css is empty or not UTF-8" },
    { "package.moduleCssUtf8", "module CSS is empty or not UTF-8: %1", "module CSS is empty or not UTF-8: {detail}" },
    { "package.combinedCssSize", "combined CSS exceeds 1 MiB", "combined CSS exceeds 1 MiB" },
    { "package.previewAsset", "preview asset is missing", "preview asset is missing" },
    { "package.assetMap", "asset map contains a non-asset ZIP name", "asset map contains a non-asset ZIP name" },
    { "package.moduleCssPath", "invalid module CSS path: %1", "invalid module CSS path: {detail}" },
    { "package.sourceResult", "invalid source theme or result", "invalid source theme or result" },
    { "package.assetPath", "invalid logical asset path", "invalid logical asset path" },
    { "package.importAsset", "cannot read imported asset or it exceeds 16 MiB", "cannot read imported asset or it exceeds 16 MiB" },
    { "package.output", "invalid theme or output", "invalid theme or output" },
    { "package.write", "cannot write theme package: %1", "cannot write theme package: {detail}" },
    { "namespace.array", "%1 must be a bounded array", "{detail} must be a bounded array" },
    { "namespace.object", "%1 item is not an object", "{detail} item is not an object" },
    { "namespace.id", "%1 has invalid or duplicate ID", "{detail} has invalid or duplicate ID" },
    { "namespace.description", "%1 has invalid description", "{detail} has invalid description" },
    { "namespace.statesMissing", "control states are missing", "control states are missing" },
    { "namespace.state", "invalid or duplicate control state", "invalid or duplicate control state" },
    { "namespace.meterUnit", "invalid meter range or unit", "invalid meter range or unit" },
    { "namespace.meterZones", "invalid meter zones", "invalid meter zones" },
    { "namespace.meterZoneObject", "meter zone is not an object", "meter zone is not an object" },
    { "namespace.meterBoundary", "invalid meter zone ID or boundary", "invalid meter zone ID or boundary" },
    { "namespace.meterMaximum", "meter zones do not reach maximum", "meter zones do not reach maximum" },
    { "namespace.overlayBoolean", "background overlay capability must be boolean", "background overlay capability must be boolean" },
    { "namespace.utf8", "descriptor is not bounded UTF-8", "descriptor is not bounded UTF-8" },
    { "namespace.json", "invalid descriptor JSON: %1", "invalid descriptor JSON: {detail}" },
    { "namespace.apiVersion", "unsupported descriptor or theme API version", "unsupported descriptor or theme API version" },
    { "namespace.identity", "invalid descriptor identity or namespace", "invalid descriptor identity or namespace" },
    { "namespace.nativeId", "Native Fx namespace does not match module ID", "Native Fx namespace does not match module ID" },
    { "namespace.nodeScope", "Node namespace must be module-scoped or shared Native Fx", "Node namespace must be module-scoped or shared Native Fx" },
    { "namespace.tokenCount", "invalid descriptor token count", "invalid descriptor token count" },
    { "namespace.tokenObject", "descriptor token is not an object", "descriptor token is not an object" },
    { "namespace.token", "invalid descriptor token: %1", "invalid descriptor token: {detail}" },
    { "namespace.parentsArray", "token inherits must be an array: %1", "token inherits must be an array: {detail}" },
    { "namespace.parent", "invalid token parent: %1", "invalid token parent: {detail}" },
    { "namespace.imageParent", "undefined image has no parent: %1", "undefined image has no parent: {detail}" },
    { "namespace.values", "token values must be an object or array: %1", "token values must be an object or array: {detail}" },
    { "namespace.inheritance", "descriptor inheritance is missing, mismatched, or cyclic: %1", "descriptor inheritance is missing, mismatched, or cyclic: {detail}" },
    { "namespace.variableType", "descriptor variable has wrong type: %1", "descriptor variable has wrong type: {detail}" },
    { "namespace.defaultType", "descriptor default has wrong type: %1", "descriptor default has wrong type: {detail}" },
    { "namespace.frozenToken", "descriptor disagrees with frozen built-in token: %1", "descriptor disagrees with frozen built-in token: {detail}" },
    { "namespace.registrationTarget", "invalid registration target", "invalid registration target" },
    { "namespace.nativeExports", "incomplete or unsupported Native Fx theme exports", "incomplete or unsupported Native Fx theme exports" },
    { "namespace.nativeMetadata", "invalid Native Fx count or metadata exports", "invalid Native Fx count or metadata exports" },
    { "namespace.nativeIndex", "Native Fx descriptor ID does not match metadata index", "Native Fx descriptor ID does not match metadata index" },
    { "namespace.nodeExports", "incomplete or unsupported Node theme exports", "incomplete or unsupported Node theme exports" },
    { "namespace.nodeCount", "invalid Node count export", "invalid Node count export" },
    { "namespace.source", "invalid descriptor source", "invalid descriptor source" },
    { "namespace.exportDuplicate", "duplicate module or namespace inside one export table", "duplicate module or namespace inside one export table" },
    { "namespace.bindingIndex", "binding index changed while still registered", "binding index changed while still registered" },
    { "namespace.owner", "conflicting module descriptor or owner: %1", "conflicting module descriptor or owner: {detail}" },
    { "namespace.namespaceOwner", "namespace already owned by another module: %1", "namespace already owned by another module: {detail}" },
};
inline bool displayKnown(const QString& source, QString* result) {
    if (source.size() > 32768) return false;
    for (const auto& rule : rules) {
        const auto original = QString::fromUtf8(rule.original);
        const auto placeholder = original.indexOf(QStringLiteral("%1"));
        if (placeholder < 0) {
            if (source == original) { *result = localizedText("theme", rule.key, rule.english); return true; }
            continue;
        }
        const auto prefix = original.left(placeholder);
        const auto suffix = original.mid(placeholder + 2);
        if (source.size() < prefix.size() + suffix.size()
            || !source.startsWith(prefix) || !source.endsWith(suffix)) continue;
        const auto detail = source.mid(prefix.size(), source.size() - prefix.size() - suffix.size());
        // Named argument formatting does not reinterpret token/path contents.
        *result = localizedText("theme", rule.key, rule.english,
                                {{QStringLiteral("detail"), detail}});
        return true;
    }
    return false;
}
inline QString display(const QString& source) {
    QString result;
    if (displayKnown(source, &result)) return result;
    if (source.size() <= 32768) {
        // A caller may prepend a canonical module/file identity to a diagnostic.
        // Preserve that identity and adapt only a recognized complete suffix.
        int wrapperAttempts = 0;
        for (auto separator = source.indexOf(QStringLiteral(": ")); separator >= 0 && wrapperAttempts++ < 8;
             separator = source.indexOf(QStringLiteral(": "), separator + 2)) {
            if (displayKnown(source.mid(separator + 2), &result))
                return source.left(separator + 2) + result;
        }
    }
    return source; // Unknown SDK/OS/user content is not relabeled as a project error.
}
}
