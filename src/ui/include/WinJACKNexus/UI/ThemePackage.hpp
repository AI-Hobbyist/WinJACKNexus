/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <WinJACKNexus/UI/Export.hpp>

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QSharedPointer>
#include <QString>
#include <QStringList>

namespace WinJACKNexus::UI
{

// An immutable, fully validated .wjnskin snapshot. No archive file handle is kept.
class WINJACK_UI_API ThemePackage final
{
  public:
    class WINJACK_UI_API ResourceRef final
    {
      public:
        bool isValid() const noexcept;
        QString packageId() const;
        QString logicalName() const;
        QByteArray bytes() const;

      private:
        friend class ThemePackage;
        QString packageId_;
        QString logicalName_;
        QSharedPointer<const QByteArray> bytes_;
    };

    ThemePackage();
    ~ThemePackage();
    ThemePackage(const ThemePackage &);
    ThemePackage &operator=(const ThemePackage &);
    ThemePackage(ThemePackage &&) noexcept;
    ThemePackage &operator=(ThemePackage &&) noexcept;

    static bool loadFromFile(const QString &path, ThemePackage *result, QString *error = nullptr);
    static bool loadFromDirectory(const QString &path, ThemePackage *result,
                                  QString *error = nullptr);
    static bool loadFromBytes(const QByteArray &zip, ThemePackage *result,
                              QString *error = nullptr);
    static bool loadDefaultFromLibrary(ThemePackage *result, QString *error = nullptr);
    static bool create(const QJsonObject &metadata, const QByteArray &styleCss,
                       const QMap<QString, QByteArray> &assets, ThemePackage *result,
                       QString *error = nullptr);
    static bool create(const QJsonObject &metadata, const QByteArray &styleCss,
                       const QMap<QString, QByteArray> &modules,
                       const QMap<QString, QByteArray> &assets, ThemePackage *result,
                       QString *error = nullptr);

    bool withImportedAsset(const QString &sourcePath, const QString &logicalName,
                           ThemePackage *result, QString *error = nullptr) const;
    bool exportToBytes(QByteArray *zip, QString *error = nullptr) const;
    bool saveToFile(const QString &path, QString *error = nullptr) const;

    bool isValid() const noexcept;
    QJsonObject metadata() const;
    QByteArray styleCss() const;
    QByteArray resource(const QString &logicalName) const;
    ResourceRef resourceRef(const QString &logicalName) const;
    QStringList resourceNames() const;
    QString id() const;

  private:
    struct Data;
    explicit ThemePackage(QSharedPointer<const Data> data);
    QSharedPointer<const Data> data_;
};

}
