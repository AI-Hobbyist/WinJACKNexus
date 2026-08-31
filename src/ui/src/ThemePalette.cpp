#include <WinJACKNexus/UI/ThemePalette.hpp>

#include <QHash>
#include <QRegularExpression>

namespace WinJACKNexus::UI {

struct ThemePalette::Impl final {
    QString name;
    QHash<QString, QColor> colors;
};

void ThemePalette::configurePalette(Impl& impl, const QString& name, bool light) {
    impl.name = name;
    const auto add = [&impl](const char* key, const char* value) {
        impl.colors.insert(QString::fromLatin1(key), QColor(QString::fromLatin1(value)));
    };

    if (light) {
        add("window-bg", "#f3f5f2");
        add("panel-bg", "#ffffff");
        add("panel-raised", "#e5ebe6");
        add("text", "#17231e");
        add("text-muted", "#5b6b62");
        add("accent", "#087f72");
        add("accent-alt", "#c47d18");
        add("border", "#b9c8be");
        add("danger", "#b83838");
        add("success", "#287d45");
    } else {
        add("window-bg", "#11181a");
        add("panel-bg", "#1b2527");
        add("panel-raised", "#263437");
        add("text", "#e7f0ec");
        add("text-muted", "#9aada5");
        add("accent", "#43c7a8");
        add("accent-alt", "#e0a04a");
        add("border", "#3b4d4d");
        add("danger", "#ee6b67");
        add("success", "#6fd08c");
    }
}

ThemePalette::ThemePalette()
    : ThemePalette(QStringLiteral("dark-rack")) {
}

ThemePalette::ThemePalette(const QString& name)
    : impl_(new Impl {}) {
    const auto normalizedName = name.trimmed().isEmpty() ? QStringLiteral("dark-rack") : name.trimmed();
    configurePalette(*impl_, normalizedName, normalizedName == QStringLiteral("light-console"));
}

ThemePalette::~ThemePalette() {
    delete impl_;
}

ThemePalette::ThemePalette(const ThemePalette& other)
    : impl_(other.impl_ != nullptr ? new Impl(*other.impl_) : nullptr) {
}

ThemePalette& ThemePalette::operator=(const ThemePalette& other) {
    if (this == &other)
        return *this;
    auto* replacement = other.impl_ != nullptr ? new Impl(*other.impl_) : nullptr;
    delete impl_;
    impl_ = replacement;
    return *this;
}

ThemePalette::ThemePalette(ThemePalette&& other) noexcept
    : impl_(other.impl_) {
    other.impl_ = nullptr;
}

ThemePalette& ThemePalette::operator=(ThemePalette&& other) noexcept {
    if (this == &other)
        return *this;
    delete impl_;
    impl_ = other.impl_;
    other.impl_ = nullptr;
    return *this;
}

ThemePalette ThemePalette::darkRack() {
    return ThemePalette(QStringLiteral("dark-rack"));
}

ThemePalette ThemePalette::lightConsole() {
    return ThemePalette(QStringLiteral("light-console"));
}

QString ThemePalette::name() const {
    return impl_ != nullptr ? impl_->name : QString {};
}

void ThemePalette::setName(const QString& name) {
    if (impl_ == nullptr)
        impl_ = new Impl {};
    impl_->name = name;
}

QColor ThemePalette::color(const QString& key) const {
    if (impl_ == nullptr)
        return {};
    return impl_->colors.value(key);
}

bool ThemePalette::setColor(const QString& key, const QColor& color) {
    if (impl_ == nullptr || key.trimmed().isEmpty() || !color.isValid())
        return false;
    impl_->colors.insert(key, color);
    return true;
}

QString ThemePalette::resolveVariables(const QString& text) const {
    if (impl_ == nullptr || text.isEmpty())
        return text;

    const QRegularExpression expression(QStringLiteral(R"(\$\{([A-Za-z0-9_.-]+)\})"));
    auto matches = expression.globalMatch(text);
    QString resolved;
    qsizetype lastPosition = 0;
    while (matches.hasNext()) {
        const auto match = matches.next();
        const auto start = match.capturedStart();
        const auto end = match.capturedEnd();
        resolved += text.mid(lastPosition, start - lastPosition);
        const auto colorValue = impl_->colors.value(match.captured(1));
        resolved += colorValue.isValid() ? colorValue.name(QColor::HexRgb) : match.captured(0);
        lastPosition = end;
    }
    resolved += text.mid(lastPosition);
    return resolved;
}

bool ThemePalette::isValid() const noexcept {
    return impl_ != nullptr;
}

}