#include <WinJACKNexus/UI/IconManager.hpp>
#include <WinJACKNexus/UI/TrayController.hpp>

#include <QApplication>
#include <QIcon>
#include <QMessageBox>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("WinJACKNexus"));
    application.setQuitOnLastWindowClosed(false);

    const QIcon applicationIcon = WinJACKNexus::UI::IconManager::icon(QStringLiteral("app"));
    application.setWindowIcon(applicationIcon);

    WinJACKNexus::UI::TrayController tray;
    if (!tray.initialize(applicationIcon, application.applicationName()))
    {
        QMessageBox::critical(nullptr, application.applicationName(),
                              QStringLiteral("当前系统环境不支持系统托盘。"));
        return 1;
    }

    QObject::connect(&tray, &WinJACKNexus::UI::TrayController::quitRequested,
                     &application, &QApplication::quit);

    return application.exec();
}