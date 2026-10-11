/* SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/NativeFx/I18n.hpp>
#include <WinJACKNexus/NativeFx/NativeFxApi.h>
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/UI/I18nManager.hpp>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QThread>
#include <algorithm>
#include <vector>

#if WJN_FX_MODULE_INDEX == 0
#include "../effects/gate/Meta.hpp"
#elif WJN_FX_MODULE_INDEX == 1
#include "../effects/graphic_eq/Meta.hpp"
#elif WJN_FX_MODULE_INDEX == 2
#include "../effects/deesser/Meta.hpp"
#elif WJN_FX_MODULE_INDEX == 3
#include "../effects/compressor/Meta.hpp"
#elif WJN_FX_MODULE_INDEX == 4
#include "../effects/limiter/Meta.hpp"
#elif WJN_FX_MODULE_INDEX == 5
#include "../effects/delay/Meta.hpp"
#elif WJN_FX_MODULE_INDEX == 6
#include "../effects/parametric_eq/Meta.hpp"
#elif WJN_FX_MODULE_INDEX == 7
#include "../effects/reverb/Meta.hpp"
#else
#error Unknown Native Fx module
#endif

namespace WinJACKNexus::NativeFx::I18n
{
namespace
{
const WjnI18nHostV1 *host = nullptr;
struct Binding
{
    QPointer<QObject> owner;
    std::function<void()> refresh;
};
std::vector<Binding> bindings;
const QByteArray domain = QByteArray("nativefx.") + activeMeta.id;
bool guiThread()
{
    const auto *app = QCoreApplication::instance();
    return app && QThread::currentThread() == app->thread();
}
void refreshBindings()
{
    std::erase_if(bindings, [](const Binding &b) { return b.owner.isNull(); });
    // A refresh may open/close an object or add a binding. Visit a stable copy.
    const auto snapshot = bindings;
    for (const auto &b : snapshot)
        if (b.owner)
            b.refresh();
}
}
QString text(const char *key, const char *english, const QVariantMap &arguments)
{
    const auto fallback = [&] {
        QString result;
        return UI::I18nManager::format(QString::fromUtf8(english), arguments, -1, &result)
                   ? result
                   : QString::fromUtf8(english);
    };
    if (!host)
        return fallback();
    const QByteArray args =
        arguments.isEmpty()
            ? QByteArray{}
            : QJsonDocument(QJsonObject::fromVariantMap(arguments)).toJson(QJsonDocument::Compact);
    const char *argsPointer = args.isEmpty() ? nullptr : args.constData();
    uint32_t size = 0;
    if (host->text(host->context, domain.constData(), key, english, argsPointer, -1, nullptr, 0,
                   &size) != WJN_I18N_OK ||
        size == 0 || size > 1024 * 1024)
        return fallback();
    QByteArray value(static_cast<qsizetype>(size), '\0');
    const auto capacity = size;
    if (host->text(host->context, domain.constData(), key, english, argsPointer, -1, value.data(),
                   capacity, &size) != WJN_I18N_OK ||
        size == 0 || size > capacity || value[size - 1] != '\0')
        return fallback();
    return QString::fromUtf8(value.constData(), size - 1);
}
void bind(QObject *owner, std::function<void()> refresh)
{
    if (!owner || !refresh || !guiThread() || owner->thread() != QThread::currentThread())
        return;
    bindings.push_back({owner, refresh});
    refresh();
}
void bindProperty(QObject *owner, const char *property, const char *key, const char *english)
{
    if (!owner)
        return;
    const QByteArray p(property), k(key), e(english);
    owner->setProperty((QByteArray("wjn.i18n.") + p).constData(), k);
    owner->setProperty((QByteArray("wjn.i18nDomain.") + p).constData(), domain);
    bind(owner, [owner, p, k, e] {
        owner->setProperty(p.constData(), text(k.constData(), e.constData()));
    });
}
}

extern "C"
{
    uint32_t wjn_fx_i18n_api_version(void)
    {
        return WJN_I18N_API_VERSION;
    }
    uint32_t wjn_get_node_i18n_api_version(void)
    {
        return WJN_I18N_API_VERSION;
    }
    const char *wjn_fx_i18n_descriptor_json(uint32_t index)
    {
        if (index != 0)
            return nullptr;
        using namespace WinJACKNexus::NativeFx::I18n;
        static const QByteArray descriptor =
            QJsonDocument(
                QJsonObject{{"descriptorVersion", 1},
                            {"i18nApiVersion", 1},
                            {"moduleId", QString::fromUtf8(activeMeta.id)},
                            {"domains", QJsonArray{QString::fromUtf8(domain)}},
                            {"locatePath", "locate"},
                            {"catalogVersion", 1},
                            {"displayNameKey", QJsonObject{{"domain", QString::fromUtf8(domain)},
                                                           {"key", "module.name"}}}})
                .toJson(QJsonDocument::Compact);
        return descriptor.constData();
    }
    const char *wjn_get_node_i18n_descriptor_json(uint32_t index)
    {
        return (activeMeta.placements & WJN_FX_NODE) ? wjn_fx_i18n_descriptor_json(index) : nullptr;
    }
    int32_t wjn_i18n_attach_host(const WjnI18nHostV1 *value)
    {
        using namespace WinJACKNexus::NativeFx::I18n;
        if (!guiThread())
            return WJN_I18N_WRONG_THREAD;
        if (!value || value->structSize < sizeof(WjnI18nHostV1) ||
            value->apiVersion != WJN_I18N_API_VERSION || !value->text || (host && host != value))
            return WJN_I18N_INVALID_ARGUMENT;
        host = value;
        return WJN_I18N_OK;
    }
    void wjn_i18n_language_changed(const char *, uint64_t)
    {
        using namespace WinJACKNexus::NativeFx::I18n;
        if (!guiThread())
            return;
        try
        {
            refreshBindings();
        }
        catch (...)
        {
        }
    }
    void wjn_i18n_detach_host(void)
    {
        using namespace WinJACKNexus::NativeFx::I18n;
        if (!guiThread())
            return;
        host = nullptr;
        // Live displays fall back to their complete local English source.
        try
        {
            refreshBindings();
        }
        catch (...)
        {
        }
    }
}
