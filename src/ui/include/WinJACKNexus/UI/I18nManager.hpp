/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/UI/Export.hpp>
#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>
#include <functional>
#include <memory>

namespace WinJACKNexus::UI
{
class I18nManager;
class WINJACK_UI_API I18nDomainLease final
{
  public:
    I18nDomainLease();
    ~I18nDomainLease();
    I18nDomainLease(I18nDomainLease &&) noexcept;
    I18nDomainLease &operator=(I18nDomainLease &&) noexcept;
    I18nDomainLease(const I18nDomainLease &) = delete;
    I18nDomainLease &operator=(const I18nDomainLease &) = delete;
    explicit operator bool() const;
    void reset();

  private:
    friend class I18nManager;
    I18nDomainLease(I18nManager *, QString);
    QPointer<I18nManager> manager_;
    QString moduleId_;
};

struct I18nOptions
{
    // Empty = existing WinJACKNexus/Application settings. Test files are isolated.
    QString settingsFile;
    // Empty = QLocale::system().uiLanguages(). Inject for deterministic tests.
    QStringList systemUiLanguages;
    bool showMissingKeys = false;
};

class WINJACK_UI_API I18nManager final : public QObject
{
    Q_OBJECT
  public:
    explicit I18nManager(I18nOptions options = {}, QObject *parent = nullptr);
    ~I18nManager() override;
    static I18nManager *installed();
    bool install(QString *error = nullptr);
    QString selectedLanguage() const;
    QString effectiveLanguage() const;
    quint64 revision() const;
    static QString resolveAutomatic(const QStringList &preferences);
    static QStringList supportedLanguages();
    bool setLanguage(const QString &selection, QString *error = nullptr);
    // Descriptor contains moduleId/domains/catalogVersion and locatePath.
    // embeddedEnglish is an optional trusted complete copy, used if English
    // on disk is absent/damaged. root must be a canonical absolute directory.
    I18nDomainLease registerModule(const QJsonObject &descriptor, const QString &root,
                                   const QByteArray &embeddedEnglish = {},
                                   QString *error = nullptr);
    // Trusted built-in manifest also survives absent/damaged external metadata.
    I18nDomainLease registerBundledModule(const QJsonObject &descriptor, const QString &root,
                                          const QByteArray &embeddedManifest,
                                          const QByteArray &embeddedEnglish,
                                          QString *error = nullptr);
    enum class Extension
    {
        node,
        nativeFx
    };
    // Pins DLL independently until last lease ends. Old modules remain usable
    // by their original API; an absent optional extension yields an empty lease.
    I18nDomainLease registerLibrary(const QString &absoluteDllPath, Extension extension,
                                    quint32 index = 0, QString *error = nullptr);
    QString text(const QString &domain, const QString &key, const QString &englishFallback,
                 const QVariantMap &arguments = {}, qint64 count = -1) const;
    // Immediate application, then each snapshot revision; context owns connection.
    QMetaObject::Connection bind(QObject *context, std::function<void()> apply);
    static bool format(const QString &source, const QVariantMap &arguments, qint64 count,
                       QString *result, QString *error = nullptr);
  Q_SIGNALS:
    void languageChanged(const QString &effectiveLanguage, quint64 revision);
    void diagnostic(const QString &code, const QString &detail);

  private:
    friend class I18nDomainLease;
    void release(const QString &moduleId);
    I18nDomainLease registerModuleImpl(const QJsonObject &, const QString &, const QByteArray &,
                                       const QByteArray &, QString *);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
