/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinJACKNexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <QApplication>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <QIcon>
#include <WinJACKNexus/VstScanner/Language.hpp>

#include <WinJACKNexus/VstScanner/MainWindow.hpp>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    WinJACKNexus::UI::ApplicationSettings::instance();
    // Restore the application's saved selection before constructing any UI.
    // Scanner settings remain separate; no language selection is written here.
    WinJACKNexus::UI::HostI18n language;
    QString languageError;
    auto scannerLanguageLease = WinJACKNexus::VstScanner::initializeLanguage(
        language, QCoreApplication::applicationDirPath(), &languageError);
    if (!scannerLanguageLease)
        qWarning("Scanner catalog initialization: %s", qPrintable(languageError));
    application.setWindowIcon(QIcon(QStringLiteral(":/WinJACKNexus/VstScanner/vst_scan.ico")));
    QStringList initialPaths;
    QString outputDirectory;
    const auto arguments = application.arguments();
    for (int index = 1; index < arguments.size(); ++index) {
        const auto argument = arguments.at(index);
        if (argument == QStringLiteral("--output") && index + 1 < arguments.size()) {
            outputDirectory = arguments.at(++index);
        } else if (argument.startsWith(QStringLiteral("--output="))) {
            outputDirectory = argument.mid(QStringLiteral("--output=").size());
        } else if (!argument.startsWith(QLatin1Char('-'))) {
            initialPaths.push_back(argument);
        }
    }

    WinJACKNexus::VstScanner::MainWindow window(initialPaths, outputDirectory);
    window.show();
    return application.exec();
}
