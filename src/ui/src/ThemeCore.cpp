/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeNamespaceRegistry.hpp>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <cmath>
#include <functional>
#include <optional>
#include <utility>

namespace WinJACKNexus::UI
{
namespace
{
bool fail(QString *error, const QString &text)
{
    if (error)
        *error = text;
    return false;
}
bool strictUtf8(const QByteArray &source, QString *result)
{
    *result = QString::fromUtf8(source);
    return result->toUtf8() == source && !result->contains(QChar::Null);
}
int lineAt(const QString &source, qsizetype offset)
{
    return 1 + source.left(offset).count('\n');
}
struct Declaration
{
    QString raw;
    int line = 0;
    ThemePackage owner;
};
struct ParsedStyle
{
    ThemePackage owner;
    QHash<QString, Declaration> declarations;
    QList<ThemeDiagnostic> diagnostics;
};
void addDiagnostic(QList<ThemeDiagnostic> *diagnostics, const QString &code, const QString &token,
                   int line, const QString &message)
{
    diagnostics->append({code, token, message, line});
}
QString moduleCssPath(const QString &scope)
{
    if (scope == "global")
        return QStringLiteral("style.css");
    if (scope == "patchbay")
        return QStringLiteral("node/patchbay.css");
    if (scope.startsWith("module-"))
        return QStringLiteral("node/") + scope + QStringLiteral(".css");
    if (scope == "native-fx")
        return QStringLiteral("native_fx/common.css");
    if (scope.startsWith("native-fx-"))
        return QStringLiteral("native_fx/") + scope.mid(10) + QStringLiteral(".css");
    return QStringLiteral("host/") + scope + QStringLiteral(".css");
}
bool parseCss(const ThemePackage &owner, const ThemeRegistry &registry, ParsedStyle *result,
              QString *error)
{
    QString source;
    if (!strictUtf8(owner.styleCss(), &source))
        return fail(error, "style.css is not UTF-8");
    if (source.size() > 1024 * 1024)
        return fail(error, "style.css exceeds 1 MiB");
    QString clean = source;
    for (qsizetype i = 0; i < clean.size();)
    {
        if (clean.mid(i, 2) == "/*")
        {
            const auto end = clean.indexOf("*/", i + 2);
            if (end < 0)
                return fail(error,
                            "unclosed CSS comment at line " + QString::number(lineAt(source, i)));
            for (auto pos = i; pos < end + 2; ++pos)
                if (clean[pos] != '\n')
                    clean[pos] = ' ';
            i = end + 2;
        }
        else if (clean.mid(i, 2) == "*/")
        {
            return fail(error,
                        "stray CSS comment end at line " + QString::number(lineAt(source, i)));
        }
        else
            ++i;
    }
    qsizetype i = 0;
    const auto skipSpace = [&] {
        while (i < clean.size() && clean[i].isSpace())
            ++i;
    };
    ParsedStyle parsed;
    parsed.owner = owner;
    QSet<QString> namespaces;
    for (const auto &entry : owner.metadata().value("namespaces").toArray())
        namespaces.insert(entry.toString());
    const QRegularExpression namePattern(QStringLiteral("^--wjn-theme-[a-z0-9-]+$"));
    bool foundRoot = false;
    while (true)
    {
        skipSpace();
        if (i == clean.size())
            break;
        if (clean.mid(i, 5) != ":root")
            return fail(error, "CSS requires :root blocks only");
        foundRoot = true;
        i += 5;
        skipSpace();
        if (i >= clean.size() || clean[i] != '{')
            return fail(error, "CSS root opening brace missing");
        const auto bodyStart = ++i;
        int parentheses = 0;
        qsizetype close = -1;
        for (; i < clean.size(); ++i)
        {
            const auto c = clean[i];
            if (c == '(')
                ++parentheses;
            else if (c == ')')
            {
                if (--parentheses < 0)
                    return fail(error, "unbalanced CSS parentheses");
            }
            else if (c == '{')
                return fail(error, "nested CSS block is forbidden");
            else if (c == '}')
            {
                close = i;
                break;
            }
        }
        if (close < 0 || parentheses != 0)
            return fail(error, "unclosed CSS root or parentheses");
        qsizetype start = bodyStart;
        for (qsizetype pos = bodyStart; pos <= close; ++pos)
        {
            if (pos != close && clean[pos] != ';')
                continue;
            const auto statement = clean.mid(start, pos - start);
            const auto colon = statement.indexOf(':');
            if (!statement.trimmed().isEmpty())
            {
                if (colon < 0)
                    return fail(error, "CSS declaration has no colon at line " +
                                           QString::number(lineAt(source, start)));
                const auto name = statement.left(colon).trimmed();
                const auto value = statement.mid(colon + 1).trimmed();
                if (!namePattern.match(name).hasMatch() || value.isEmpty() || value.contains('{') ||
                    value.contains('}'))
                    return fail(error, "invalid CSS declaration at line " +
                                           QString::number(lineAt(source, start)));
                const auto line = lineAt(source, start + statement.indexOf(name));
                if (!registry.contains(name))
                {
                    addDiagnostic(&parsed.diagnostics, "unknown-token", name, line,
                                  "Unknown token retained in package but ignored for drawing");
                }
                else
                {
                    const auto token = registry.token(name);
                    const auto scope = QStringLiteral("--wjn-theme-") + token.ownerScope + '-';
                    if (!namespaces.contains(scope))
                        return fail(error, "token outside declared namespace at line " +
                                               QString::number(line));
                    if (parsed.declarations.contains(name))
                        addDiagnostic(&parsed.diagnostics, "duplicate-token", name, line,
                                      "Later declaration overrides earlier declaration");
                    parsed.declarations.insert(name, {value, line, owner});
                }
            }
            start = pos + 1;
        }
        i = close + 1;
    }
    if (!foundRoot)
        return fail(error, "CSS requires at least one :root block");
    *result = std::move(parsed);
    return true;
}
bool resourcePath(const QString &path)
{
    if (!path.startsWith("assets/") || path.contains('\\') || path.contains(':') ||
        path.contains('?') || path.contains('#') || path.endsWith('/'))
        return false;
    const auto pieces = path.split('/');
    for (const auto &piece : pieces)
        if (piece.isEmpty() || piece == "." || piece == "..")
            return false;
    return true;
}
bool numeric(const QString &value, double *output)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)$"));
    if (!pattern.match(value).hasMatch())
        return false;
    bool okay = false;
    *output = value.toDouble(&okay);
    return okay && std::isfinite(*output);
}
bool validColor(const QString &value)
{
    if (value == "transparent")
        return true;
    static const QRegularExpression hex(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    if (hex.match(value).hasMatch())
        return true;
    const bool rgba = value.startsWith("rgba(") && value.endsWith(')');
    const bool rgb = value.startsWith("rgb(") && value.endsWith(')');
    if (!rgb && !rgba)
        return false;
    const auto inside = value.mid(rgba ? 5 : 4, value.size() - (rgba ? 6 : 5));
    const auto parts = inside.split(',');
    if (parts.size() != (rgba ? 4 : 3))
        return false;
    static const QRegularExpression integer(QStringLiteral("^[0-9]+$"));
    for (int i = 0; i < 3; ++i)
    {
        const auto part = parts[i].trimmed();
        if (!integer.match(part).hasMatch())
            return false;
        bool okay = false;
        const auto channel = part.toInt(&okay);
        if (!okay || channel < 0 || channel > 255)
            return false;
    }
    if (rgba)
    {
        double alpha = 0;
        if (!numeric(parts[3].trimmed(), &alpha) || alpha < 0 || alpha > 1)
            return false;
    }
    return true;
}
bool validValue(const QString &type, const QString &value, QString *assetPath)
{
    if (type == "color")
        return validColor(value);
    if (type == "boolean")
        return value == "true" || value == "false";
    if (type == "opacity")
    {
        double number = 0;
        return numeric(value, &number) && number >= 0 && number <= 1;
    }
    if (type == "position")
    {
        if (!value.endsWith('%') && !value.endsWith("px"))
            return false;
        double number = 0;
        return numeric(value.left(value.size() - (value.endsWith("px") ? 2 : 1)), &number);
    }
    if (type == "blur-radius")
    {
        if (!value.endsWith("px"))
            return false;
        double number = 0;
        return numeric(value.chopped(2), &number) && number >= 0 && number <= 64;
    }
    if (type == "background-scale")
    {
        double number = 0;
        return numeric(value, &number) && number >= 0.05 && number <= 4.0;
    }
    if (type == "background-size")
        return QStringList{"original", "contain", "cover", "stretch"}.contains(value);
    if (type == "background-repeat")
        return QStringList{"no-repeat", "repeat", "repeat-x", "repeat-y"}.contains(value);
    if (type == "image" && value == "none")
        return true;
    if (type == "font" && value == "system")
        return true;
    if (type != "image" && type != "font")
        return false;
    if (!value.startsWith("url(\"assets/") || !value.endsWith("\")"))
        return false;
    const auto path = value.mid(5, value.size() - 7);
    if (!resourcePath(path))
        return false;
    const auto suffix = path.mid(path.lastIndexOf('.')).toLower();
    const bool validSuffix = type == "font"
                                 ? (suffix == ".ttf" || suffix == ".otf")
                                 : QStringList{".png", ".jpg", ".jpeg", ".webp"}.contains(suffix);
    if (validSuffix && assetPath)
        *assetPath = path;
    return validSuffix;
}
struct ValueResult
{
    QString value;
    ThemePackage::ResourceRef resource;
    bool found = false;
};
class Resolver
{
  public:
    Resolver(const ThemeRegistry &registry, const std::optional<ParsedStyle> &full,
             const QList<ParsedStyle> &fragments, const QHash<QString, Declaration> &overrides,
             QList<ThemeDiagnostic> *diagnostics)
        : registry_(registry), full_(full), fragments_(fragments), overrides_(overrides),
          diagnostics_(diagnostics)
    {
    }

    ValueResult resolve(const QString &name)
    {
        QSet<QString> active;
        return token(name, true, active);
    }

  private:
    ValueResult token(const QString &name, bool allowDefault, QSet<QString> &active)
    {
        if (!registry_.contains(name) || active.contains(name))
        {
            if (active.contains(name))
                addDiagnostic(diagnostics_, "reference-cycle", name, 0, "Circular token reference");
            return {};
        }
        active.insert(name);
        const auto declaration = [&](const Declaration &item) -> ValueResult {
            ValueResult directReference;
            const auto expanded = variables(item.raw, active, 0, &directReference);
            if (!expanded)
            {
                addDiagnostic(diagnostics_, "invalid-value", name, item.line,
                              "Unresolved variable reference");
                return {};
            }
            QString path;
            const auto definition = registry_.token(name);
            if (!validValue(definition.type, expanded->trimmed(), &path))
            {
                addDiagnostic(diagnostics_, "invalid-value", name, item.line,
                              "Value does not match registered type " + definition.type);
                return {};
            }
            ValueResult result{expanded->trimmed(), {}, true};
            if (!path.isEmpty())
            {
                if (directReference.found && directReference.value == result.value)
                    result.resource = directReference.resource;
                else if (item.owner.isValid())
                    result.resource = item.owner.resourceRef(path);
                if (!result.resource.isValid())
                    addDiagnostic(diagnostics_, "missing-resource", name, item.line,
                                  "Resource is absent for the resolved token: " + path);
            }
            return result;
        };
        ValueResult resolved;
        if (const auto found = overrides_.constFind(name); found != overrides_.cend())
            resolved = declaration(found.value());
        if (!resolved.found)
            for (auto it = fragments_.crbegin(); it != fragments_.crend(); ++it)
            {
                if (const auto found = it->declarations.constFind(name);
                    found != it->declarations.cend())
                {
                    resolved = declaration(found.value());
                    if (resolved.found)
                        break;
                }
            }
        if (!resolved.found && full_)
        {
            if (const auto found = full_->declarations.constFind(name);
                found != full_->declarations.cend())
                resolved = declaration(found.value());
        }
        const auto definition = registry_.token(name);
        if (!resolved.found)
            for (const auto &parent : definition.inherits)
            {
                resolved = token(parent, false, active);
                if (resolved.found)
                    break;
            }
        if (!resolved.found && allowDefault && definition.hasDefault)
            resolved = declaration({definition.defaultValue, 0, {}});
        if (!resolved.found && allowDefault)
            for (const auto &parent : definition.inherits)
            {
                resolved = token(parent, true, active);
                if (resolved.found)
                    break;
            }
        active.remove(name);
        return resolved;
    }
    std::optional<QString> variables(const QString &raw, QSet<QString> &active, int depth,
                                     ValueResult *directReference)
    {
        if (depth > 32 || raw.size() > 4096)
            return std::nullopt;
        const auto begin = raw.indexOf("var(");
        if (begin < 0)
            return raw;
        int nesting = 1;
        qsizetype end = begin + 4;
        for (; end < raw.size() && nesting; ++end)
        {
            if (raw[end] == '(')
                ++nesting;
            else if (raw[end] == ')')
                --nesting;
        }
        if (nesting != 0)
            return std::nullopt;
        const auto content = raw.mid(begin + 4, end - begin - 5);
        int nested = 0;
        qsizetype comma = -1;
        for (qsizetype i = 0; i < content.size(); ++i)
        {
            if (content[i] == '(')
                ++nested;
            else if (content[i] == ')')
                --nested;
            else if (content[i] == ',' && nested == 0)
            {
                comma = i;
                break;
            }
        }
        const auto reference = (comma < 0 ? content : content.left(comma)).trimmed();
        static const QRegularExpression pattern(QStringLiteral("^--wjn-theme-[a-z0-9-]+$"));
        if (!pattern.match(reference).hasMatch())
            return std::nullopt;
        const auto referenced = token(reference, true, active);
        std::optional<QString> replacement;
        if (referenced.found)
        {
            replacement = referenced.value;
            if (directReference && raw.left(begin).trimmed().isEmpty() &&
                raw.mid(end).trimmed().isEmpty())
                *directReference = referenced;
        }
        else if (comma >= 0)
            replacement =
                variables(content.mid(comma + 1).trimmed(), active, depth + 1, directReference);
        if (!replacement)
            return std::nullopt;
        return variables(raw.left(begin) + *replacement + raw.mid(end), active, depth + 1,
                         directReference);
    }
    const ThemeRegistry &registry_;
    const std::optional<ParsedStyle> &full_;
    const QList<ParsedStyle> &fragments_;
    const QHash<QString, Declaration> &overrides_;
    QList<ThemeDiagnostic> *diagnostics_;
};
} // namespace

bool ThemeRegistry::fromJson(const QByteArray &json, ThemeRegistry *result, QString *error)
{
    if (!result)
        return fail(error, "null registry result");
    const auto document = QJsonDocument::fromJson(json);
    if (!document.isObject() || document.object().value("registryVersion").toInt(-1) != 1 ||
        document.object().value("themeApiVersion").toInt(-1) != 1)
        return fail(error, "invalid theme registry version");
    const auto entries = document.object().value("tokens").toArray();
    if (entries.isEmpty() || entries.size() > 8192)
        return fail(error, "invalid registry token count");
    ThemeRegistry candidate;
    const QRegularExpression namePattern(QStringLiteral("^--wjn-theme-[a-z0-9-]+$"));
    const QRegularExpression scopePattern(QStringLiteral("^[a-z0-9]+(?:-[a-z0-9]+)*$"));
    const QSet<QString> types{
        "color", "opacity",         "boolean",           "position",    "image",
        "font",  "background-size", "background-repeat", "blur-radius", "background-scale"};
    for (const auto &value : entries)
    {
        if (!value.isObject())
            return fail(error, "registry token is not an object");
        const auto object = value.toObject();
        ThemeToken item;
        item.name = object.value("name").toString();
        item.ownerScope = object.value("ownerScope").toString();
        item.type = object.value("type").toString();
        item.description = object.value("description").toString();
        item.hasDefault = object.value("default").isString();
        item.defaultValue = object.value("default").toString();
        if (!namePattern.match(item.name).hasMatch() ||
            !scopePattern.match(item.ownerScope).hasMatch() ||
            !item.name.startsWith("--wjn-theme-" + item.ownerScope + '-') ||
            !types.contains(item.type) || item.description.isEmpty() ||
            (item.hasDefault && (item.defaultValue.isEmpty() || item.defaultValue.size() > 4096)) ||
            (!item.hasDefault && (item.type != "image" || !object.value("default").isNull())) ||
            candidate.tokens_.contains(item.name))
            return fail(error, "invalid or duplicate registry token: " + item.name);
        const auto parents = object.value("inherits");
        if (!parents.isArray())
            return fail(error, "registry inherits must be an array: " + item.name);
        for (const auto &parent : parents.toArray())
        {
            if (!parent.isString() || item.inherits.contains(parent.toString()))
                return fail(error, "invalid duplicate registry parent: " + item.name);
            item.inherits.append(parent.toString());
        }
        if (!item.hasDefault && item.inherits.isEmpty())
            return fail(error, "undefined image token requires an inheritance source");
        candidate.tokens_.insert(item.name, item);
    }
    QSet<QString> completed;
    std::function<bool(const QString &, QSet<QString> &)> validate = [&](const QString &name,
                                                                         QSet<QString> &active) {
        if (completed.contains(name))
            return true;
        if (active.contains(name))
            return false;
        active.insert(name);
        const auto item = candidate.tokens_.value(name);
        for (const auto &parent : item.inherits)
        {
            if (!candidate.tokens_.contains(parent) ||
                candidate.tokens_.value(parent).type != item.type || !validate(parent, active))
                return false;
        }
        active.remove(name);
        completed.insert(name);
        return true;
    };
    for (const auto &name : candidate.tokens_.keys())
    {
        QSet<QString> active;
        if (!validate(name, active))
            return fail(error, "registry inheritance is missing, mismatched, or cyclic: " + name);
    }
    *result = std::move(candidate);
    if (error)
        error->clear();
    return true;
}
bool ThemeRegistry::builtIn(ThemeRegistry *result, QString *error)
{
    QFile file(QStringLiteral(":/WinJACKNexus/UI/themes/v1-registry.json"));
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, "built-in theme registry is unavailable");
    return fromJson(file.readAll(), result, error);
}
bool ThemeRegistry::valueMatchesType(const QString &type, const QString &value)
{
    return validValue(type, value, nullptr);
}
bool ThemeRegistry::contains(const QString &token) const
{
    return tokens_.contains(token);
}
ThemeToken ThemeRegistry::token(const QString &name) const
{
    return tokens_.value(name);
}
QStringList ThemeRegistry::names() const
{
    auto result = tokens_.keys();
    result.sort();
    return result;
}
bool ThemeRegistry::withTokens(const QList<ThemeToken> &additions, ThemeRegistry *result,
                               QString *error) const
{
    if (!result)
        return fail(error, "null extended registry result");
    QHash<QString, ThemeToken> all = tokens_;
    for (const auto &token : additions)
    {
        const auto existing = all.constFind(token.name);
        if (existing != all.cend())
        {
            if (existing->ownerScope != token.ownerScope || existing->type != token.type ||
                existing->hasDefault != token.hasDefault ||
                existing->defaultValue != token.defaultValue ||
                existing->inherits != token.inherits)
                return fail(error, "module token conflicts with registered token: " + token.name);
            continue;
        }
        all.insert(token.name, token);
    }
    QJsonArray entries;
    QStringList names = all.keys();
    names.sort();
    for (const auto &name : names)
    {
        const auto &token = all[name];
        QJsonArray parents;
        for (const auto &parent : token.inherits)
            parents.append(parent);
        entries.append(QJsonObject{{"name", token.name},
                                   {"ownerScope", token.ownerScope},
                                   {"type", token.type},
                                   {"default", token.hasDefault ? QJsonValue(token.defaultValue)
                                                                : QJsonValue(QJsonValue::Null)},
                                   {"inherits", parents},
                                   {"description", token.description}});
    }
    return fromJson(
        QJsonDocument(
            QJsonObject{{"registryVersion", 1}, {"themeApiVersion", 1}, {"tokens", entries}})
            .toJson(QJsonDocument::Compact),
        result, error);
}

QString ThemeSnapshot::themeId() const
{
    return themeId_;
}
bool ThemeSnapshot::hasValue(const QString &token) const
{
    return entries_.contains(token);
}
QString ThemeSnapshot::value(const QString &token) const
{
    return entries_.value(token).value;
}
ThemePackage::ResourceRef ThemeSnapshot::resource(const QString &token) const
{
    return entries_.value(token).resource;
}
QList<ThemeDiagnostic> ThemeSnapshot::diagnostics() const
{
    return diagnostics_;
}

struct ThemeCore::State
{
    std::optional<ParsedStyle> full;
    QList<ParsedStyle> fragments;
    QHash<QString, Declaration> overrides;
};
ThemeCore::ThemeCore(QObject *parent) : QObject(parent)
{
    if (ThemeRegistry::builtIn(&baseRegistry_, &initializationError_))
    {
        registry_ = baseRegistry_;
        if (ThemePackage::loadDefaultFromLibrary(&defaultTheme_, &initializationError_))
            setFullTheme(defaultTheme_, &initializationError_);
    }
}
ThemeCore::ThemeCore(ThemeRegistry registry, QObject *parent)
    : QObject(parent), registry_(registry), baseRegistry_(std::move(registry))
{
    if (ThemePackage::loadDefaultFromLibrary(&defaultTheme_, &initializationError_))
        setFullTheme(defaultTheme_, &initializationError_);
}
bool ThemeCore::isValid() const noexcept
{
    return bool(snapshot_);
}
QString ThemeCore::initializationError() const
{
    return initializationError_;
}
ThemePackage ThemeCore::defaultTheme() const
{
    return defaultTheme_;
}
QSharedPointer<const ThemeSnapshot> ThemeCore::snapshot() const
{
    return snapshot_;
}
QSharedPointer<const ThemeSnapshot> ThemeCore::previewBaseSnapshot() const
{
    return previewPreviousSnapshot_;
}
bool ThemeCore::activate(State state, QString *error)
{
    if (registry_.names().isEmpty())
        return fail(error, "theme registry is empty");
    auto next = QSharedPointer<ThemeSnapshot>::create();
    if (!state.full)
        return fail(error, "required default theme package is unavailable");
    next->themeId_ = state.full->owner.id();
    if (state.full)
        next->diagnostics_ += state.full->diagnostics;
    for (const auto &fragment : state.fragments)
        next->diagnostics_ += fragment.diagnostics;
    Resolver resolver(registry_, state.full, state.fragments, state.overrides, &next->diagnostics_);
    for (const auto &name : registry_.names())
    {
        const auto value = resolver.resolve(name);
        if (value.found)
            next->entries_.insert(name, {value.value, value.resource});
    }
    state_ = QSharedPointer<State>::create(std::move(state));
    snapshot_ = next;
    if (error)
        error->clear();
    emit themeChanged();
    return true;
}
bool ThemeCore::setFullTheme(const ThemePackage &package, QString *error)
{
    if (!package.isValid() || package.metadata().value("kind") != "full")
        return fail(error, "expected a valid full theme package");
    ParsedStyle parsed;
    if (!parseCss(package, registry_, &parsed, error))
        return false;
    State state;
    state.full = std::move(parsed);
    return activate(std::move(state), error);
}
bool ThemeCore::applyFragment(const ThemePackage &package, QString *error)
{
    if (!package.isValid() || package.metadata().value("kind") != "fragment")
        return fail(error, "expected a valid fragment package");
    ParsedStyle parsed;
    if (!parseCss(package, registry_, &parsed, error))
        return false;
    State state = state_ ? *state_ : State{};
    for (auto it = state.fragments.begin(); it != state.fragments.end();)
    {
        if (it->owner.id() == package.id())
            it = state.fragments.erase(it);
        else
            ++it;
    }
    state.fragments.append(std::move(parsed));
    return activate(std::move(state), error);
}
bool ThemeCore::setOverride(const QString &name, const QString &value,
                            const ThemePackage &resourceOwner, QString *error)
{
    if (!registry_.contains(name))
        return fail(error, "unknown override token");
    State state = state_ ? *state_ : State{};
    state.overrides.insert(name, {value, 0,
                                  resourceOwner.isValid()
                                      ? resourceOwner
                                      : (state.full ? state.full->owner : ThemePackage{})});
    return activate(std::move(state), error);
}
bool ThemeCore::removeOverride(const QString &name)
{
    if (!state_ || !state_->overrides.contains(name))
        return false;
    State state = *state_;
    state.overrides.remove(name);
    return activate(std::move(state), nullptr);
}
bool ThemeCore::beginPreview(const ThemePackage &package, QString *error)
{
    if (previewPrevious_)
        return fail(error, "preview already active");
    auto previous = QSharedPointer<State>::create(state_ ? *state_ : State{});
    previewPrevious_ = std::move(previous);
    previewPreviousSnapshot_ = snapshot_;
    if (!setFullTheme(package, error))
    {
        previewPrevious_.clear();
        previewPreviousSnapshot_.clear();
        return false;
    }
    return true;
}
bool ThemeCore::cancelPreview()
{
    if (!previewPrevious_)
        return false;
    const auto previous = *previewPrevious_;
    previewPrevious_.clear();
    previewPreviousSnapshot_.clear();
    return activate(previous, nullptr);
}
bool ThemeCore::commitPreview()
{
    if (!previewPrevious_)
        return false;
    previewPrevious_.clear();
    previewPreviousSnapshot_.clear();
    emit previewCompleted();
    return true;
}
void ThemeCore::restoreDefault()
{
    previewPrevious_.clear();
    previewPreviousSnapshot_.clear();
    if (defaultTheme_.isValid())
        setFullTheme(defaultTheme_, nullptr);
}
ThemePackage ThemeCore::activeFullTheme() const
{
    return state_ && state_->full ? state_->full->owner : ThemePackage{};
}
QString ThemeCore::declaredValue(const QString &name) const
{
    if (!state_)
        return {};
    if (const auto found = state_->overrides.constFind(name); found != state_->overrides.cend())
        return found->raw;
    for (auto it = state_->fragments.crbegin(); it != state_->fragments.crend(); ++it)
        if (const auto found = it->declarations.constFind(name); found != it->declarations.cend())
            return found->raw;
    if (state_->full)
        if (const auto found = state_->full->declarations.constFind(name);
            found != state_->full->declarations.cend())
            return found->raw;
    return {};
}
bool ThemeCore::exportFullTheme(const QJsonObject &metadata, ThemePackage *result,
                                QString *error) const
{
    return exportPackage({}, metadata, result, error);
}
bool ThemeCore::exportFragment(const QString &namespacePrefix, const QJsonObject &metadata,
                               ThemePackage *result, QString *error) const
{
    static const QRegularExpression pattern(QStringLiteral("^--wjn-theme-[a-z0-9-]+-$"));
    if (!pattern.match(namespacePrefix).hasMatch())
        return fail(error, "invalid fragment namespace");
    return exportPackage(namespacePrefix, metadata, result, error);
}
bool ThemeCore::hasUserOverrides() const
{
    return state_ && !state_->overrides.isEmpty();
}
bool ThemeCore::exportUserOverrides(ThemePackage *result, QString *error) const
{
    if (!result || !state_)
        return fail(error, "theme override export has no output or active state");
    QStringList names = state_->overrides.keys();
    names.sort();
    QStringList declarations;
    QSet<QString> namespaces;
    QMap<QString, QByteArray> assets;
    const QRegularExpression urlPattern(QStringLiteral("url\\(\"(assets/[^\"]+)\"\\)"));
    for (const auto &name : names)
    {
        if (!registry_.contains(name))
            continue;
        namespaces.insert(QStringLiteral("--wjn-theme-") + registry_.token(name).ownerScope + '-');
        const auto &source = state_->overrides.value(name);
        QString value = source.raw;
        auto iterator = urlPattern.globalMatch(value);
        while (iterator.hasNext())
        {
            const auto match = iterator.next();
            const auto logical = match.captured(1);
            const auto bytes = source.owner.resource(logical);
            if (bytes.isNull())
                return fail(error, "override resource is missing: " + logical);
            const auto target = QStringLiteral("assets/override-%1/%2")
                                    .arg(declarations.size())
                                    .arg(logical.mid(7));
            assets.insert(target, bytes);
            value.replace(match.captured(0), QStringLiteral("url(\"%1\")").arg(target));
        }
        declarations.append(QStringLiteral("  %1: %2;").arg(name, value));
    }
    if (namespaces.isEmpty())
        namespaces.insert(QStringLiteral("--wjn-theme-global-"));
    QStringList sorted = namespaces.values();
    sorted.sort();
    QJsonArray namespaceArray;
    for (const auto &item : sorted)
        namespaceArray.append(item);
    const QJsonObject meta{{"formatVersion", 1},
                           {"themeApiVersion", 1},
                           {"id", "org.winjacknexus-user-overrides"},
                           {"name", "User overrides"},
                           {"version", "1.0.0"},
                           {"author", "WinJACKNexus"},
                           {"description", "Per-theme user preferences"},
                           {"homepage", ""},
                           {"kind", "fragment"},
                           {"namespaces", namespaceArray}};
    const auto css = QStringLiteral(":root {\n%1\n}\n").arg(declarations.join('\n')).toUtf8();
    return ThemePackage::create(meta, css, assets, result, error);
}
bool ThemeCore::restoreUserOverrides(const ThemePackage &package, QString *error)
{
    if (!package.isValid() || package.metadata().value("kind") != "fragment")
        return fail(error, "expected a user override fragment");
    ParsedStyle parsed;
    if (!parseCss(package, registry_, &parsed, error))
        return false;
    State state = state_ ? *state_ : State{};
    state.overrides = std::move(parsed.declarations);
    return activate(std::move(state), error);
}
bool ThemeCore::exportPackage(const QString &namespacePrefix, const QJsonObject &metadata,
                              ThemePackage *result, QString *error) const
{
    if (!result || !state_)
        return fail(error, "theme export has no active state or output");
    const bool fragment = !namespacePrefix.isEmpty();
    QJsonObject output = state_->full->owner.metadata();
    for (auto it = metadata.begin(); it != metadata.end(); ++it)
        output.insert(it.key(), it.value());
    if (fragment)
    {
        if (!metadata.contains("id"))
            output.insert("id", output.value("id").toString() + QStringLiteral("-fragment-") +
                                    namespacePrefix.mid(12, namespacePrefix.size() - 13));
        if (!metadata.contains("name"))
            output.insert("name", output.value("name").toString() + QStringLiteral(" / ") +
                                      namespacePrefix);
    }
    output.insert("kind", fragment ? QStringLiteral("fragment") : QStringLiteral("full"));
    QSet<QString> namespaces;
    if (fragment)
        namespaces.insert(namespacePrefix);
    else
    {
        const auto addNamespaces = [&](const ThemePackage &owner) {
            for (const auto &item : owner.metadata().value("namespaces").toArray())
                namespaces.insert(item.toString());
        };
        if (state_->full)
            addNamespaces(state_->full->owner);
        for (const auto &item : state_->fragments)
            addNamespaces(item.owner);
    }
    QStringList declarations;
    QMap<QString, QByteArray> assets;
    int sourceIndex = 0;
    const QRegularExpression commentPattern(QStringLiteral("/\\*[\\s\\S]*?\\*/"));
    const QRegularExpression urlPattern(QStringLiteral("url\\(\"(assets/[^\"]+)\"\\)"));
    const QRegularExpression tokenPattern(QStringLiteral("^--wjn-theme-[a-z0-9-]+$"));
    const auto appendSource = [&](const ThemePackage &owner, int index) -> bool {
        QString source = QString::fromUtf8(owner.styleCss());
        source.remove(commentPattern);
        source.replace(QRegularExpression(QStringLiteral("\\}\\s*:root\\s*\\{")),
                       QStringLiteral(";"));
        const auto opening = source.indexOf('{');
        const auto closing = source.lastIndexOf('}');
        if (opening < 0 || closing <= opening)
            return fail(error, "source theme CSS is invalid");
        const auto declared = owner.metadata().value("namespaces").toArray();
        for (const auto &raw : source.mid(opening + 1, closing - opening - 1).split(';'))
        {
            const auto colon = raw.indexOf(':');
            if (colon < 0)
                continue;
            const auto token = raw.left(colon).trimmed();
            if (!tokenPattern.match(token).hasMatch())
                continue;
            if (registry_.contains(token))
                continue; // Effective values are written below.
            QString owningNamespace;
            if (registry_.contains(token))
                owningNamespace =
                    QStringLiteral("--wjn-theme-") + registry_.token(token).ownerScope + '-';
            else
                for (const auto &entry : declared)
                {
                    const auto prefix = entry.toString();
                    if (token.startsWith(prefix) && prefix.size() > owningNamespace.size())
                        owningNamespace = prefix;
                }
            if (fragment && owningNamespace != namespacePrefix)
                continue;
            if (!fragment && !owningNamespace.isEmpty())
                namespaces.insert(owningNamespace);
            QString value = raw.mid(colon + 1).trimmed();
            const auto matches = urlPattern.globalMatch(value);
            auto cursor = 0;
            QString rewritten;
            auto iterator = matches;
            while (iterator.hasNext())
            {
                const auto match = iterator.next();
                const auto logical = match.captured(1);
                const auto bytes = owner.resource(logical);
                if (bytes.isNull())
                    return fail(error, "export resource is missing: " + logical);
                const auto target =
                    QStringLiteral("assets/source-%1/%2").arg(index).arg(logical.mid(7));
                assets.insert(target, bytes);
                rewritten += value.mid(cursor, match.capturedStart() - cursor) +
                             QStringLiteral("url(\"%1\")").arg(target);
                cursor = match.capturedEnd();
            }
            rewritten += value.mid(cursor);
            declarations.append(QStringLiteral("  %1: %2;").arg(token, rewritten));
        }
        return true;
    };
    if (state_->full && !appendSource(state_->full->owner, sourceIndex++))
        return false;
    for (const auto &item : state_->fragments)
        if (!appendSource(item.owner, sourceIndex++))
            return false;
    for (const auto &name : registry_.names())
    {
        const auto definition = registry_.token(name);
        const auto owningNamespace = QStringLiteral("--wjn-theme-") + definition.ownerScope + '-';
        if (fragment && owningNamespace != namespacePrefix)
            continue;
        if (!snapshot_->hasValue(name))
            continue;
        namespaces.insert(owningNamespace);
        QString value = snapshot_->value(name);
        const auto resource = snapshot_->resource(name);
        if (resource.isValid())
        {
            const auto target = QStringLiteral("assets/effective/%1/%2")
                                    .arg(name.mid(12), resource.logicalName().mid(7));
            assets.insert(target, resource.bytes());
            value = QStringLiteral("url(\"%1\")").arg(target);
        }
        declarations.append(QStringLiteral("  %1: %2;").arg(name, value));
    }
    if (output.contains("preview"))
    {
        const auto original = output.value("preview").toString();
        const auto previewOwner = state_->full ? state_->full->owner : ThemePackage{};
        const auto bytes = previewOwner.resource(original);
        if (bytes.isNull())
            output.remove("preview");
        else
        {
            const auto target = QStringLiteral("assets/preview/") + original.mid(7);
            assets.insert(target, bytes);
            output.insert("preview", target);
        }
    }
    if (fragment && declarations.isEmpty())
        return fail(error, "selected fragment namespace has no declarations");
    QStringList sortedNamespaces = namespaces.values();
    sortedNamespaces.sort();
    QJsonArray namespaceArray;
    for (const auto &item : sortedNamespaces)
        namespaceArray.append(item);
    output.insert("namespaces", namespaceArray);
    QMap<QString, QStringList> grouped;
    for (const auto &declaration : declarations)
    {
        const auto name = declaration.left(declaration.indexOf(':')).trimmed();
        QString scope;
        if (registry_.contains(name))
            scope = registry_.token(name).ownerScope;
        else
            for (const auto &prefix : sortedNamespaces)
            {
                if (name.startsWith(prefix) && prefix.size() > scope.size() + 12)
                    scope = prefix.mid(12, prefix.size() - 13);
            }
        if (scope.isEmpty())
            return fail(error, "export declaration has no module scope: " + name);
        grouped[moduleCssPath(scope)].append(declaration);
    }
    const auto cssText = [](const QStringList &lines) {
        return QStringLiteral(":root {\n%1\n}\n").arg(lines.join('\n')).toUtf8();
    };
    const auto globalCss = cssText(grouped.take(QStringLiteral("style.css")));
    QMap<QString, QByteArray> modules;
    for (auto it = grouped.cbegin(); it != grouped.cend(); ++it)
        modules.insert(it.key(), cssText(it.value()));
    return ThemePackage::create(output, globalCss, modules, assets, result, error);
}
bool ThemeCore::refreshRegisteredNamespaces(QString *error)
{
    QList<ThemeToken> additions;
    const auto &modules = ThemeNamespaceRegistry::instance();
    for (const auto &id : modules.moduleIds())
        if (const auto descriptor = modules.descriptor(id))
            additions.append(descriptor->tokens);
    ThemeRegistry updated;
    if (!baseRegistry_.withTokens(additions, &updated, error))
        return false;
    State state = state_ ? *state_ : State{};
    if (state.full)
    {
        ParsedStyle parsed;
        if (parseCss(state.full->owner, updated, &parsed, nullptr))
            state.full = std::move(parsed);
    }
    for (auto &fragment : state.fragments)
    {
        ParsedStyle parsed;
        if (parseCss(fragment.owner, updated, &parsed, nullptr))
            fragment = std::move(parsed);
    }
    registry_ = std::move(updated);
    return activate(std::move(state), error);
}
}
