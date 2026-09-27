/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/UI/ThemeNamespaceRegistry.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>

#include <WinJACKNexus/NativeFx/NativeFxApi.h>
#include <WinJACKNexus/Node/NodeApi.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>

#include <cmath>
#include <cstring>
#include <functional>
#include <utility>

namespace WinJACKNexus::UI {
namespace {
constexpr size_t maxDescriptorBytes = 1024 * 1024;
constexpr int maxTokens = 4096;

bool fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}
bool stableId(const QString& value) {
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9]+(?:-[a-z0-9]+)*$"));
    return value.size() >= 1 && value.size() <= 96 && pattern.match(value).hasMatch();
}
bool tokenName(const QString& value) {
    static const QRegularExpression pattern(QStringLiteral("^--wjn-theme-[a-z0-9-]+$"));
    return pattern.match(value).hasMatch();
}
bool validText(const QString& value, int maximum = 2048) {
    return !value.trimmed().isEmpty() && value.size() <= maximum
        && !value.contains(QChar::Null);
}
bool semanticArray(const QJsonObject& object, const char* field, QString* error) {
    if (!object.contains(QLatin1StringView(field))) return true;
    const auto value = object.value(QLatin1StringView(field));
    if (!value.isArray() || value.toArray().size() > maxTokens)
        return fail(error, QString::fromLatin1(field) + " must be a bounded array");
    QSet<QString> ids;
    for (const auto& item : value.toArray()) {
        if (!item.isObject()) return fail(error, QString::fromLatin1(field) + " item is not an object");
        const auto entry = item.toObject();
        const auto id = entry.value("id").toString();
        if (!stableId(id) || ids.contains(id))
            return fail(error, QString::fromLatin1(field) + " has invalid or duplicate ID");
        ids.insert(id);
        if (entry.contains("description") && !validText(entry.value("description").toString()))
            return fail(error, QString::fromLatin1(field) + " has invalid description");
        if (QLatin1StringView(field) == QLatin1StringView("controls")) {
            const auto states = entry.value("states");
            if (!states.isArray() || states.toArray().isEmpty())
                return fail(error, "control states are missing");
            QSet<QString> seen;
            const QSet<QString> allowed {"normal", "hover", "pressed", "selected", "disabled", "focus"};
            for (const auto& state : states.toArray()) {
                const auto name = state.toString();
                if (!allowed.contains(name) || seen.contains(name))
                    return fail(error, "invalid or duplicate control state");
                seen.insert(name);
            }
        }
        if (QLatin1StringView(field) == QLatin1StringView("meterStyles")) {
            const auto lower = entry.value("minimum").toDouble(qQNaN());
            const auto upper = entry.value("maximum").toDouble(qQNaN());
            if (!std::isfinite(lower) || !std::isfinite(upper) || lower >= upper
                || !validText(entry.value("unit").toString(), 32))
                return fail(error, "invalid meter range or unit");
            const auto zones = entry.value("zones");
            if (!zones.isArray() || zones.toArray().isEmpty() || zones.toArray().size() > 64)
                return fail(error, "invalid meter zones");
            double previous = lower;
            QSet<QString> zoneIds;
            for (const auto& zoneValue : zones.toArray()) {
                if (!zoneValue.isObject()) return fail(error, "meter zone is not an object");
                const auto zone = zoneValue.toObject();
                const auto zoneId = zone.value("id").toString();
                const auto end = zone.value("upper").toDouble(qQNaN());
                if (!stableId(zoneId) || zoneIds.contains(zoneId) || !std::isfinite(end)
                    || end <= previous || end > upper)
                    return fail(error, "invalid meter zone ID or boundary");
                zoneIds.insert(zoneId);
                previous = end;
            }
            if (previous != upper) return fail(error, "meter zones do not reach maximum");
        }
        if (QLatin1StringView(field) == QLatin1StringView("backgroundAreas")
            && entry.contains("overlay") && !entry.value("overlay").isBool())
            return fail(error, "background overlay capability must be boolean");
    }
    return true;
}
bool descriptorFromJson(const QByteArray& bytes, ThemeModuleKind kind,
                        QSharedPointer<const ThemeNamespaceDescriptor>* result, QString* error) {
    if (bytes.isEmpty() || bytes.size() > static_cast<qsizetype>(maxDescriptorBytes)
        || QString::fromUtf8(bytes).toUtf8() != bytes || bytes.contains('\0'))
        return fail(error, "descriptor is not bounded UTF-8");
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (!document.isObject())
        return fail(error, "invalid descriptor JSON: " + parseError.errorString());
    const auto object = document.object();
    if (object.value("descriptorVersion").toInt(-1) != 1
        || object.value("themeApiVersion").toInt(-1) != 1)
        return fail(error, "unsupported descriptor or theme API version");
    const auto moduleId = object.value("moduleId").toString();
    const auto nameSpace = object.value("namespace").toString();
    const auto displayName = object.value("displayName").toString();
    if (!stableId(moduleId) || !validText(displayName, 128)
        || nameSpace.size() > 160 || !nameSpace.startsWith("--wjn-theme-")
        || !nameSpace.endsWith('-') || !tokenName(nameSpace.left(nameSpace.size() - 1)))
        return fail(error, "invalid descriptor identity or namespace");
    const auto nativeScope = QStringLiteral("--wjn-theme-native-fx-") + moduleId + '-';
    if (kind == ThemeModuleKind::nativeFx && nameSpace != nativeScope)
        return fail(error, "Native Fx namespace does not match module ID");
    if (kind == ThemeModuleKind::node && nameSpace != nativeScope
        && !nameSpace.startsWith("--wjn-theme-module-"))
        return fail(error, "Node namespace must be module-scoped or shared Native Fx");
    const auto tokenValues = object.value("tokens");
    if (!tokenValues.isArray() || tokenValues.toArray().isEmpty()
        || tokenValues.toArray().size() > maxTokens)
        return fail(error, "invalid descriptor token count");
    if (!semanticArray(object, "controls", error)
        || !semanticArray(object, "chartSeries", error)
        || !semanticArray(object, "meterStyles", error)
        || !semanticArray(object, "backgroundAreas", error)) return false;

    ThemeRegistry builtIn;
    if (!ThemeRegistry::builtIn(&builtIn, error)) return false;
    auto descriptor = QSharedPointer<ThemeNamespaceDescriptor>::create();
    descriptor->moduleId = moduleId;
    descriptor->namespacePrefix = nameSpace;
    descriptor->displayName = displayName;
    descriptor->metadata = object;
    descriptor->canonicalJson = document.toJson(QJsonDocument::Compact);
    QHash<QString, ThemeToken> local;
    const auto scope = nameSpace.mid(QStringLiteral("--wjn-theme-").size(),
                                     nameSpace.size() - QStringLiteral("--wjn-theme-").size() - 1);
    const QSet<QString> types {"color", "opacity", "boolean", "position", "image", "font",
                               "background-size", "background-repeat"};
    for (const auto& value : tokenValues.toArray()) {
        if (!value.isObject()) return fail(error, "descriptor token is not an object");
        const auto objectToken = value.toObject();
        ThemeToken token;
        token.name = objectToken.value("name").toString();
        token.type = objectToken.value("type").toString();
        token.description = objectToken.value("description").toString();
        token.ownerScope = scope;
        token.hasDefault = objectToken.value("default").isString();
        token.defaultValue = objectToken.value("default").toString();
        if (!tokenName(token.name) || !token.name.startsWith(nameSpace)
            || local.contains(token.name) || !types.contains(token.type)
            || !validText(token.description)
            || !objectToken.contains("default")
            || (!token.hasDefault && (token.type != "image" || !objectToken.value("default").isNull()))
            || (token.hasDefault && (token.defaultValue.isEmpty() || token.defaultValue.size() > 4096)))
            return fail(error, "invalid descriptor token: " + token.name);
        const auto parentValues = objectToken.value("inherits");
        if (!parentValues.isUndefined() && !parentValues.isArray())
            return fail(error, "token inherits must be an array: " + token.name);
        for (const auto& parent : parentValues.toArray()) {
            const auto parentName = parent.toString();
            if (!tokenName(parentName) || token.inherits.contains(parentName))
                return fail(error, "invalid token parent: " + token.name);
            token.inherits.append(parentName);
        }
        if (!token.hasDefault && token.inherits.isEmpty())
            return fail(error, "undefined image has no parent: " + token.name);
        if (objectToken.contains("values") && !objectToken.value("values").isObject()
            && !objectToken.value("values").isArray())
            return fail(error, "token values must be an object or array: " + token.name);
        local.insert(token.name, token);
        descriptor->tokens.append(token);
    }
    const auto lookup = [&](const QString& name) -> ThemeToken {
        return local.contains(name) ? local.value(name) : builtIn.token(name);
    };
    QSet<QString> finished;
    std::function<bool(const QString&, QSet<QString>&)> checkParents =
        [&](const QString& name, QSet<QString>& active) {
            if (finished.contains(name)) return true;
            if (active.contains(name)) return false;
            active.insert(name);
            const auto token = local.value(name);
            for (const auto& parent : token.inherits) {
                const auto target = lookup(parent);
                if (target.name.isEmpty() || target.type != token.type
                    || (local.contains(parent) && !checkParents(parent, active))) return false;
            }
            active.remove(name);
            finished.insert(name);
            return true;
        };
    static const QRegularExpression variable(QStringLiteral("^var\\(\\s*(--wjn-theme-[a-z0-9-]+)(?:\\s*,\\s*(.+))?\\)$"));
    for (const auto& token : descriptor->tokens) {
        QSet<QString> active;
        if (!checkParents(token.name, active))
            return fail(error, "descriptor inheritance is missing, mismatched, or cyclic: " + token.name);
        if (!token.hasDefault) continue;
        const auto match = variable.match(token.defaultValue);
        if (match.hasMatch()) {
            const auto target = lookup(match.captured(1));
            if (target.name.isEmpty() || target.type != token.type
                || (!match.captured(2).isEmpty()
                    && !ThemeRegistry::valueMatchesType(token.type, match.captured(2).trimmed())))
                return fail(error, "descriptor variable has wrong type: " + token.name);
        } else if (!ThemeRegistry::valueMatchesType(token.type, token.defaultValue)) {
            return fail(error, "descriptor default has wrong type: " + token.name);
        }
        const auto existing = builtIn.token(token.name);
        if (!existing.name.isEmpty() && (existing.type != token.type
            || existing.defaultValue != token.defaultValue
            || existing.hasDefault != token.hasDefault || existing.inherits != token.inherits))
            return fail(error, "descriptor disagrees with frozen built-in token: " + token.name);
    }
    *result = descriptor;
    if (error) error->clear();
    return true;
}

QByteArray boundedJson(const char* text) {
    if (!text) return {};
    const auto bytes = strnlen_s(text, maxDescriptorBytes + 1);
    return bytes > maxDescriptorBytes ? QByteArray {} : QByteArray(text, static_cast<qsizetype>(bytes));
}
}

ThemeNamespaceRegistry::Lease::~Lease() { reset(); }
ThemeNamespaceRegistry::Lease::Lease(Lease&& other) noexcept
    : registry_(std::exchange(other.registry_, nullptr)), keys_(std::move(other.keys_)) {}
ThemeNamespaceRegistry::Lease& ThemeNamespaceRegistry::Lease::operator=(Lease&& other) noexcept {
    if (this != &other) {
        reset();
        registry_ = std::exchange(other.registry_, nullptr);
        keys_ = std::move(other.keys_);
    }
    return *this;
}
void ThemeNamespaceRegistry::Lease::reset() {
    if (registry_) registry_->release(keys_);
    registry_ = nullptr;
    keys_.clear();
}

ThemeNamespaceRegistry& ThemeNamespaceRegistry::instance() {
    static auto* registry = new ThemeNamespaceRegistry;
    return *registry;
}
QString ThemeNamespaceRegistry::bindingKey(ThemeModuleKind kind, const QString& source, quint32 index) {
    return QString::number(static_cast<int>(kind)) + QChar(0x1f) + source.toCaseFolded()
        + QChar(0x1f) + QString::number(index);
}
bool ThemeNamespaceRegistry::registerNativeFxModule(const ThemeSymbolResolver& resolve,
                                                     const QString& source, Lease* result, QString* error) {
    if (!resolve || !result || !result->isEmpty()) return fail(error, "invalid registration target");
    using Count = uint32_t (*)();
    using Meta = const WjnFxMeta* (*)(uint32_t);
    using Json = const char* (*)(uint32_t);
    const auto version = reinterpret_cast<Count>(resolve("wjn_fx_theme_api_version"));
    const auto json = reinterpret_cast<Json>(resolve("wjn_fx_theme_namespace_json"));
    if (!version && !json) { if (error) error->clear(); return true; }
    if (!version || !json || version() != WJN_FX_THEME_API_VERSION)
        return fail(error, "incomplete or unsupported Native Fx theme exports");
    const auto count = reinterpret_cast<Count>(resolve("wjn_fx_count"));
    const auto meta = reinterpret_cast<Meta>(resolve("wjn_fx_meta"));
    if (!count || !meta || count() > maxTokens)
        return fail(error, "invalid Native Fx count or metadata exports");
    std::vector<Candidate> candidates;
    for (uint32_t index = 0, length = count(); index < length; ++index) {
        const auto* raw = json(index);
        if (!raw) continue;
        QSharedPointer<const ThemeNamespaceDescriptor> descriptor;
        if (!descriptorFromJson(boundedJson(raw), ThemeModuleKind::nativeFx, &descriptor, error)) return false;
        const auto* module = meta(index);
        if (!module || !module->id || descriptor->moduleId != QString::fromUtf8(module->id))
            return fail(error, "Native Fx descriptor ID does not match metadata index");
        candidates.push_back({bindingKey(ThemeModuleKind::nativeFx, source, index), descriptor});
    }
    return registerCandidates(candidates, source, result, error);
}
bool ThemeNamespaceRegistry::registerNodeModule(const ThemeSymbolResolver& resolve,
                                                 const QString& source, Lease* result, QString* error) {
    if (!resolve || !result || !result->isEmpty()) return fail(error, "invalid registration target");
    using Count = uint32_t (*)();
    using Json = const char* (*)(uint32_t);
    const auto version = reinterpret_cast<Count>(resolve("wjn_get_node_theme_api_version"));
    const auto json = reinterpret_cast<Json>(resolve("wjn_get_node_theme_namespace_json"));
    if (!version && !json) { if (error) error->clear(); return true; }
    if (!version || !json || version() != WJN_NODE_THEME_API_VERSION)
        return fail(error, "incomplete or unsupported Node theme exports");
    const auto count = reinterpret_cast<Count>(resolve("wjn_get_node_count"));
    if (!count || count() > maxTokens)
        return fail(error, "invalid Node count export");
    std::vector<Candidate> candidates;
    for (uint32_t index = 0, length = count(); index < length; ++index) {
        const auto* raw = json(index);
        if (!raw) continue;
        QSharedPointer<const ThemeNamespaceDescriptor> descriptor;
        if (!descriptorFromJson(boundedJson(raw), ThemeModuleKind::node, &descriptor, error)) return false;
        candidates.push_back({bindingKey(ThemeModuleKind::node, source, index), descriptor});
    }
    return registerCandidates(candidates, source, result, error);
}
bool ThemeNamespaceRegistry::registerInternal(const QByteArray& utf8Json, const QString& source,
                                               Lease* result, QString* error) {
    if (!result || !result->isEmpty()) return fail(error, "invalid registration target");
    QSharedPointer<const ThemeNamespaceDescriptor> descriptor;
    if (!descriptorFromJson(utf8Json, ThemeModuleKind::internal, &descriptor, error)) return false;
    return registerCandidates({{bindingKey(ThemeModuleKind::internal, source, 0), descriptor}},
                              source, result, error);
}
bool ThemeNamespaceRegistry::registerCandidates(const std::vector<Candidate>& candidates,
                                                 const QString& source, Lease* result, QString* error) {
    if (source.trimmed().isEmpty() || source.contains(QChar(0x1f)))
        return fail(error, "invalid descriptor source");
    const auto normalizedSource = source.toCaseFolded();
    QMutexLocker lock(&mutex_);
    QSet<QString> batchIds;
    QSet<QString> batchNamespaces;
    for (const auto& candidate : candidates) {
        const auto& descriptor = candidate.descriptor;
        if (batchIds.contains(descriptor->moduleId)
            || batchNamespaces.contains(descriptor->namespacePrefix))
            return fail(error, "duplicate module or namespace inside one export table");
        batchIds.insert(descriptor->moduleId);
        batchNamespaces.insert(descriptor->namespacePrefix);
        const auto binding = bindings_.value(candidate.bindingKey);
        if (binding.references && binding.moduleId != descriptor->moduleId)
            return fail(error, "binding index changed while still registered");
        const auto existing = entries_.value(descriptor->moduleId);
        if (existing.references && (existing.descriptor->namespacePrefix != descriptor->namespacePrefix
            || existing.descriptor->canonicalJson != descriptor->canonicalJson))
            return fail(error, "conflicting module descriptor or owner: " + descriptor->moduleId);
        for (auto it = entries_.cbegin(); it != entries_.cend(); ++it)
            if (it.key() != descriptor->moduleId
                && it.value().descriptor->namespacePrefix == descriptor->namespacePrefix)
                return fail(error, "namespace already owned by another module: " + descriptor->namespacePrefix);
    }
    for (const auto& candidate : candidates) {
        const auto& id = candidate.descriptor->moduleId;
        auto& entry = entries_[id];
        if (!entry.references) {
            entry.descriptor = candidate.descriptor;
            entry.source = normalizedSource;
        }
        ++entry.references;
        auto& binding = bindings_[candidate.bindingKey];
        binding.moduleId = id;
        ++binding.references;
        result->keys_.append(candidate.bindingKey);
    }
    result->registry_ = this;
    if (error) error->clear();
    lock.unlock();
    if (this == &instance())
        if (auto* core = ThemeManager::installedThemeCore()) core->refreshRegisteredNamespaces();
    return true;
}
void ThemeNamespaceRegistry::release(const QStringList& keys) {
    QMutexLocker lock(&mutex_);
    for (const auto& key : keys) {
        auto binding = bindings_.find(key);
        if (binding == bindings_.end()) continue;
        const auto id = binding->moduleId;
        if (--binding->references == 0) bindings_.erase(binding);
        auto entry = entries_.find(id);
        if (entry != entries_.end() && --entry->references == 0) entries_.erase(entry);
    }
    lock.unlock();
    if (this == &instance())
        if (auto* core = ThemeManager::installedThemeCore()) core->refreshRegisteredNamespaces();
}
QSharedPointer<const ThemeNamespaceDescriptor> ThemeNamespaceRegistry::descriptor(const QString& moduleId) const {
    QMutexLocker lock(&mutex_);
    return entries_.value(moduleId).descriptor;
}
QString ThemeNamespaceRegistry::scopeFor(ThemeModuleKind kind, const QString& source, quint32 index) const {
    QMutexLocker lock(&mutex_);
    const auto binding = bindings_.value(bindingKey(kind, source, index));
    return binding.references ? entries_.value(binding.moduleId).descriptor->namespacePrefix : QString {};
}
int ThemeNamespaceRegistry::referenceCount(const QString& moduleId) const {
    QMutexLocker lock(&mutex_);
    return entries_.value(moduleId).references;
}
QStringList ThemeNamespaceRegistry::moduleIds() const {
    QMutexLocker lock(&mutex_);
    auto names = entries_.keys();
    names.sort();
    return names;
}
}
