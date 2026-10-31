/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <WinJACKNexus/Common/Export.hpp>
#include <QtGlobal>

class QProcess;
class QString;

namespace WinJACKNexus::Common {

// Windows priority-class values; invalid saved values resolve to Above Normal.
WINJACK_COMMON_API bool isProcessPriorityClass(quint32 priorityClass) noexcept;
WINJACK_COMMON_API quint32 configuredProcessPriorityClass();
// Reject invalid classes, OS errors and silent Windows downgrades.
WINJACK_COMMON_API bool applyProcessPriorityClass(quint32 priorityClass, QString* error = nullptr);
// Read the global preference at each launch, including scanner grandchildren.
WINJACK_COMMON_API void configureChildProcessPriority(QProcess& process);

}
