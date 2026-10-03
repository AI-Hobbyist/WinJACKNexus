/* Private built-in module display bridge. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <functional>

namespace WinJACKNexus::NativeFx::I18n {
// Compiled into each effect DLL. The host table and bindings never cross DLLs.
QString text(const char* key, const char* english, const QVariantMap& arguments = {});
void bind(QObject* owner, std::function<void()> refresh);
void bindProperty(QObject* owner, const char* property, const char* key, const char* english);
}
