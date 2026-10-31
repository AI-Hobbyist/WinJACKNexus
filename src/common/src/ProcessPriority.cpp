/* SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/Common/ProcessPriority.hpp>

#include <QProcess>
#include <QSettings>
#include <QString>
#include <Windows.h>

namespace WinJACKNexus::Common {

bool isProcessPriorityClass(quint32 priorityClass) noexcept
{
    switch (priorityClass) {
    case IDLE_PRIORITY_CLASS:
    case BELOW_NORMAL_PRIORITY_CLASS:
    case NORMAL_PRIORITY_CLASS:
    case ABOVE_NORMAL_PRIORITY_CLASS:
    case HIGH_PRIORITY_CLASS:
    case REALTIME_PRIORITY_CLASS:
        return true;
    default:
        return false;
    }
}

quint32 configuredProcessPriorityClass()
{
    const auto value = QSettings(QSettings::defaultFormat(), QSettings::UserScope,
        QStringLiteral("WinJACKNexus"), QStringLiteral("Application"))
        .value(QStringLiteral("processPriorityClass"), ABOVE_NORMAL_PRIORITY_CLASS).toUInt();
    return isProcessPriorityClass(value) ? value : ABOVE_NORMAL_PRIORITY_CLASS;
}

bool applyProcessPriorityClass(quint32 priorityClass, QString* error)
{
    if (error) error->clear();
    if (!isProcessPriorityClass(priorityClass)) {
        if (error) *error = QStringLiteral("Unsupported Windows process priority class.");
        return false;
    }
    const auto process = GetCurrentProcess();
    const auto previous = GetPriorityClass(process);
    if (!SetPriorityClass(process, priorityClass)) {
        const auto code = GetLastError();
        if (error) *error = QStringLiteral("SetPriorityClass failed (Windows error %1).").arg(code);
        return false;
    }
    if (GetPriorityClass(process) != priorityClass) {
        if (previous) SetPriorityClass(process, previous);
        if (error) *error = QStringLiteral("Windows did not grant the requested process priority.");
        return false;
    }
    return true;
}

void configureChildProcessPriority(QProcess& process)
{
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) {
        constexpr DWORD classes = IDLE_PRIORITY_CLASS | BELOW_NORMAL_PRIORITY_CLASS
            | NORMAL_PRIORITY_CLASS | ABOVE_NORMAL_PRIORITY_CLASS | HIGH_PRIORITY_CLASS
            | REALTIME_PRIORITY_CLASS;
        arguments->flags = (arguments->flags & ~classes) | configuredProcessPriorityClass();
    });
}

}
