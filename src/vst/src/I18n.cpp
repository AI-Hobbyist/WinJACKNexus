/* SPDX-License-Identifier: AGPL-3.0-only */
#include "I18n.hpp"
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/Common/I18nApi.h>
#include <QCoreApplication>
#include <QPointer>
#include <QThread>
#include <QVariant>
#include <algorithm>
#include <vector>

namespace VstI18n
{
namespace
{
const WjnI18nHostV1 *host = nullptr;
struct Binding
{
    QPointer<QObject> owner;
    std::function<void()> apply;
};
std::vector<Binding> bindings;
bool guiThread()
{
    const auto *app = QCoreApplication::instance();
    return app && QThread::currentThread() == app->thread();
}
void refresh()
{
    std::erase_if(bindings, [](const Binding &item) { return item.owner.isNull(); });
    const auto snapshot = bindings;
    for (const auto &item : snapshot)
        if (item.owner)
            item.apply();
}
}
QString Text::string() const
{
    QString result = QString::fromUtf8(english_);
    if (host && !key_.isEmpty())
    {
        uint32_t size = 0;
        if (host->text(host->context, "vst", key_.constData(), english_.constData(), nullptr, -1,
                       nullptr, 0, &size) == WJN_I18N_OK &&
            size > 0 && size <= 1024 * 1024)
        {
            QByteArray bytes(static_cast<qsizetype>(size), '\0');
            const auto capacity = size;
            if (host->text(host->context, "vst", key_.constData(), english_.constData(), nullptr,
                           -1, bytes.data(), capacity, &size) == WJN_I18N_OK &&
                size > 0 && size <= capacity && bytes[size - 1] == '\0')
                result = QString::fromUtf8(bytes.constData(), size - 1);
        }
    }
    for (const auto &value : values_)
        result = result.arg(value);
    return result;
}
void bind(QObject *owner, std::function<void()> apply)
{
    if (!owner || !apply || !guiThread() || owner->thread() != QThread::currentThread())
        return;
    bindings.push_back({owner, apply});
    apply();
}
void property(QObject *owner, const char *property, const Text &value)
{
    if (!owner)
        return;
    const QByteArray name(property);
    bind(owner, [owner, name, value] { owner->setProperty(name.constData(), value.string()); });
}
}

uint32_t wjn_get_node_i18n_api_version(void)
{
    return WJN_I18N_API_VERSION;
}
const char *wjn_get_node_i18n_descriptor_json(uint32_t index)
{
    static constexpr char descriptor[] =
        R"({"descriptorVersion":1,"i18nApiVersion":1,"moduleId":"module-winjacknexus-vst-host","domains":["vst"],"locatePath":"locate","catalogVersion":1,"displayNameKey":{"domain":"vst","key":"module.name"}})";
    return index == 0 ? descriptor : nullptr;
}
int32_t wjn_i18n_attach_host(const WjnI18nHostV1 *value)
{
    if (!VstI18n::guiThread())
        return WJN_I18N_WRONG_THREAD;
    if (!value || value->structSize < sizeof(WjnI18nHostV1) ||
        value->apiVersion != WJN_I18N_API_VERSION || !value->text ||
        (VstI18n::host && VstI18n::host != value))
        return WJN_I18N_INVALID_ARGUMENT;
    VstI18n::host = value;
    return WJN_I18N_OK;
}
void wjn_i18n_language_changed(const char *, uint64_t)
{
    if (!VstI18n::guiThread())
        return;
    try
    {
        VstI18n::refresh();
    }
    catch (...)
    {
    }
}
void wjn_i18n_detach_host(void)
{
    if (!VstI18n::guiThread())
        return;
    VstI18n::host = nullptr;
    try
    {
        VstI18n::refresh();
    }
    catch (...)
    {
    }
}
