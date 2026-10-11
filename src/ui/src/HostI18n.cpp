/* SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/UI/HostI18n.hpp>
#include <QActionGroup>
#include <QComboBox>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QSignalBlocker>

namespace WinJACKNexus::UI
{
namespace
{
QPointer<HostI18n> host;
const QStringList selections{QStringLiteral("auto"), QStringLiteral("zh-CN"),
                             QStringLiteral("en-US"), QStringLiteral("ja-JP"),
                             QStringLiteral("ko-KR")};
QByteArray resource(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
}
QString hostText(const char *key, const char *english, const QVariantMap &arguments)
{
    const auto *manager = I18nManager::installed();
    if (manager)
        return manager->text(QStringLiteral("host"), QString::fromUtf8(key),
                             QString::fromUtf8(english), arguments);
    QString result;
    return I18nManager::format(QString::fromUtf8(english), arguments, -1, &result)
               ? result
               : QString::fromUtf8(english);
}
void bindHostText(QObject *object, const char *property, const char *key, const char *english)
{
    if (!object)
        return;
    const QByteArray p(property), k(key), e(english);
    object->setProperty((QByteArray("wjn.i18n.") + p).constData(), k);
    const auto apply = [object, p, k, e] {
        object->setProperty(p.constData(), hostText(k.constData(), e.constData()));
    };
    if (auto *manager = I18nManager::installed())
        manager->bind(object, apply);
    else
        apply();
}
HostI18n::HostI18n(I18nOptions options, QObject *parent)
    : QObject(parent), manager_(std::move(options))
{
}
HostI18n::~HostI18n()
{
    // The host lease publishes removal during member destruction. Disconnect
    // first: qtTranslator_ is destroyed before hostLease_.
    disconnect(&manager_, nullptr, this, nullptr);
    if (qtInstalled_)
        QCoreApplication::removeTranslator(&qtTranslator_);
    if (host == this)
        host.clear();
}
bool HostI18n::initialize(const QString &root, QString *error)
{
    if (hostLease_)
        return true;
    if (!manager_.install(error))
        return false;
    const auto manifest = resource(QStringLiteral(":/WinJACKNexus/UI/i18n/manifest.json"));
    const auto metadata = QJsonDocument::fromJson(manifest).object();
    QJsonObject descriptor{
        {QStringLiteral("descriptorVersion"), 1},
        {QStringLiteral("i18nApiVersion"), 1},
        {QStringLiteral("moduleId"), metadata[QStringLiteral("moduleId")]},
        {QStringLiteral("domains"), metadata[QStringLiteral("domains")]},
        {QStringLiteral("catalogVersion"), metadata[QStringLiteral("catalogVersion")]},
        {QStringLiteral("locatePath"), QStringLiteral("locate")}};
    hostLease_ = manager_.registerBundledModule(
        descriptor, root, manifest, resource(QStringLiteral(":/WinJACKNexus/UI/i18n/en-US.json")),
        error);
    if (!hostLease_)
        return false;
    root_ = root;
    host = this;
    // Established before application windows bind languageChanged.
    connect(&manager_, &I18nManager::languageChanged, this,
            [this](const QString &, quint64) { updateQtTranslation(); });
    updateQtTranslation();
    return true;
}
HostI18n *HostI18n::installed()
{
    return host.data();
}
I18nManager &HostI18n::manager()
{
    return manager_;
}
QString HostI18n::languageDisplayName(const QString &selection)
{
    if (selection == QStringLiteral("zh-CN"))
        return QStringLiteral("简体中文");
    if (selection == QStringLiteral("ja-JP"))
        return QStringLiteral("日本語");
    if (selection == QStringLiteral("ko-KR"))
        return QStringLiteral("한국어");
    if (selection == QStringLiteral("en-US"))
        return QStringLiteral("English");
    const auto *current = I18nManager::installed();
    return hostText(
        "language.autoEffective", "Automatic ({language})",
        {{QStringLiteral("language"),
          languageDisplayName(current ? current->effectiveLanguage() : QStringLiteral("en-US"))}});
}
QMenu *HostI18n::createLanguageMenu(QWidget *parent)
{
    auto *menu = new QMenu(parent);
    menu->setObjectName(QStringLiteral("hostLanguageMenu"));
    auto *group = new QActionGroup(menu);
    group->setExclusive(true);
    for (const auto &selection : selections)
    {
        auto *action = menu->addAction(languageDisplayName(selection));
        action->setData(selection);
        action->setCheckable(true);
        group->addAction(action);
        action->setObjectName(QStringLiteral("host.language.") + selection);
        connect(action, &QAction::triggered, this, [this, group, selection] {
            QString error;
            if (!manager_.setLanguage(selection, &error))
            {
                for (auto *item : group->actions())
                    item->setChecked(item->data().toString() == manager_.selectedLanguage());
                Q_EMIT languageSelectionFailed(error);
            }
        });
    }
    manager_.bind(menu, [menu, group, this] {
        menu->setTitle(hostText("menu.language", "Language"));
        for (auto *item : group->actions())
        {
            item->setText(languageDisplayName(item->data().toString()));
            item->setChecked(item->data().toString() == manager_.selectedLanguage());
        }
    });
    return menu;
}
void HostI18n::bindLanguageChoice(QComboBox *choice)
{
    if (!choice)
        return;
    {
        const QSignalBlocker block(choice);
        choice->clear();
        for (const auto &selection : selections)
            choice->addItem(languageDisplayName(selection), selection);
    }
    manager_.bind(choice, [choice, this] {
        const QSignalBlocker block(choice);
        for (int i = 0; i < choice->count(); ++i)
            choice->setItemText(i, languageDisplayName(choice->itemData(i).toString()));
        choice->setCurrentIndex(choice->findData(manager_.selectedLanguage()));
    });
    connect(choice, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, choice](int index) {
                if (index < 0)
                    return;
                QString error;
                if (!manager_.setLanguage(choice->itemData(index).toString(), &error))
                {
                    const QSignalBlocker block(choice);
                    choice->setCurrentIndex(choice->findData(manager_.selectedLanguage()));
                    Q_EMIT languageSelectionFailed(error);
                }
            });
}
void HostI18n::updateQtTranslation()
{
    if (qtInstalled_)
    {
        QCoreApplication::removeTranslator(&qtTranslator_);
        qtInstalled_ = false;
    }
    const auto language = manager_.effectiveLanguage();
    const auto suffix = language == QStringLiteral("zh-CN")   ? QStringLiteral("zh_CN")
                        : language == QStringLiteral("ja-JP") ? QStringLiteral("ja")
                        : language == QStringLiteral("ko-KR") ? QStringLiteral("ko")
                                                              : QString{};
    if (suffix.isEmpty())
        return;
    const auto filename = QStringLiteral("qtbase_") + suffix + QStringLiteral(".qm");
    if (qtTranslator_.load(root_ + QStringLiteral("/locate/qt/") + filename) ||
        qtTranslator_.load(QStringLiteral(":/WinJACKNexus/UI/i18n/qt/") + filename))
        qtInstalled_ = QCoreApplication::installTranslator(&qtTranslator_);
}
}
