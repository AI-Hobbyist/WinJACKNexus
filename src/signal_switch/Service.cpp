/* SPDX-License-Identifier: AGPL-3.0-only */
#include "Service.hpp"
#include "Sound.hpp"
#include <algorithm>
#include <cmath>

namespace WinJACKNexus::SignalSwitch
{
Service &Service::instance()
{
    static Service service;
    return service;
}

void Service::attach(void *node, Endpoint endpoint)
{
    const bool added = !state.cards.contains(endpoint.id);
    state.add(endpoint.id);
    if (added && (endpoint.peak || endpoint.gainLinear))
        state.cards[endpoint.id].geometry.setSize(endpoint.peak ? QSize(300, 120) : QSize(180, 120));
    endpoints.insert(node, std::move(endpoint));
    reconcile();
    notify();
}

void Service::detach(void *node)
{
    auto it = endpoints.find(node);
    if (it == endpoints.end())
        return;
    if (!restoring_)
        state.remove(it->id);
    endpoints.erase(it);
    reconcile();
    notify();
}

void Service::bind(void *node, const QString &id, const QString &name, const QString &context)
{
    auto it = endpoints.find(node);
    if (it == endpoints.end())
        return;
    if (it->id != id)
    {
        state.remove(it->id);
        state.add(id);
        it->id = id;
    }
    it->name = name;
    it->context = context;
    notify();
}

Endpoint *Service::find(const QString &id)
{
    for (auto &endpoint : endpoints)
    {
        if (endpoint.id == id)
            return &endpoint;
    }
    return nullptr;
}

bool Service::setEnabled(const QStringList &ids, bool enabled, QString *error)
{
    for (const auto &id : ids)
    {
        if (const auto *endpoint = find(id); endpoint && (endpoint->sound || endpoint->peak || endpoint->gainLinear))
            return false;
    }
    QMap<QString, bool> current;
    for (const auto &endpoint : endpoints)
        current[endpoint.id] = endpoint.desired;
    QMap<QString, bool> next;
    if (!state.resolve(current, ids, enabled, &next, error))
        return false;
    for (auto &endpoint : endpoints)
        endpoint.desired = next.value(endpoint.id);
    reconcile();
    notify(true);
    return true;
}

void Service::activate(const QString &id)
{
    if (auto *endpoint = find(id))
    {
        if (endpoint->sound)
            endpoint->sound->trigger();
        else if (endpoint->gainLinear && endpoint->editValue)
            endpoint->editValue();
        else if (!endpoint->peak && !endpoint->gainLinear)
            setEnabled({id}, !endpoint->desired);
    }
}

bool Service::setGain(const QString &id, double db)
{
    auto *endpoint = find(id);
    if (!endpoint || !endpoint->gainLinear || !std::isfinite(db) || db < endpoint->gainMinimum ||
        db > endpoint->gainMaximum)
        return false;
    endpoint->gainDb = db;
    endpoint->gainLinear->store(float(std::pow(10.0, db / 20.0)), std::memory_order_relaxed);
    notify(true);
    return true;
}

bool Service::setGainRange(const QString &id, double minimum, double maximum)
{
    auto *endpoint = find(id);
    if (!endpoint || !endpoint->gainLinear || !std::isfinite(minimum) || !std::isfinite(maximum) || minimum < -720 ||
        maximum > 720 || minimum >= maximum)
        return false;
    endpoint->gainMinimum = minimum;
    endpoint->gainMaximum = maximum;
    return setGain(id, std::clamp(endpoint->gainDb, minimum, maximum));
}

void Service::pollSounds()
{
    bool edited = false;
    for (const auto &endpoint : endpoints)
        if (endpoint.sound)
            edited |= endpoint.sound->poll();
    if (edited)
        notify(true);
}

bool Service::restoreDesired(void *node, bool enabled)
{
    auto it = endpoints.find(node);
    if (it != endpoints.end() && restoring_)
    {
        it->desired = enabled;
        return true;
    }
    return it != endpoints.end() && setEnabled({it->id}, enabled);
}

void Service::reconcile()
{
    if (restoring_)
    {
        for (auto &endpoint : endpoints)
            endpoint.gate->request(false);
        return;
    }
    // First publish every close. Opening is permitted only after all peers confirm quiet.
    for (auto &endpoint : endpoints)
    {
        if (!endpoint.desired)
            endpoint.gate->request(false);
    }
    for (auto &endpoint : endpoints)
    {
        if (!endpoint.desired)
            continue;
        bool ready = true;
        for (const auto &members : state.exclusive)
        {
            if (!members.contains(endpoint.id))
                continue;
            for (const auto &id : members)
            {
                const auto *peer = find(id);
                if (peer && peer != &endpoint && !peer->gate->quiet())
                    ready = false;
            }
        }
        endpoint.gate->request(ready);
    }
}

void Service::notify(bool edit)
{
    if (changed)
        changed();
    if (edit && !restoring_ && edited)
        edited();
}

bool Service::restore(const QString &base64, QString *error)
{
    State candidate;
    if (!base64.isEmpty() && !State::fromBase64(base64, &candidate, error))
        return false;
    // Reject conflicting saved gate states instead of silently selecting a winner.
    for (const auto &members : candidate.exclusive)
    {
        int enabled = 0;
        for (const auto &id : members)
        {
            const auto *endpoint = find(id);
            if (endpoint && (endpoint->sound || endpoint->peak || endpoint->gainLinear))
            {
                if (error)
                    *error = QStringLiteral("Sound buttons cannot use switch exclusivity");
                return false;
            }
            enabled += endpoint && endpoint->desired;
        }
        if (enabled > 1)
        {
            if (error)
                *error = QStringLiteral("Conflicting saved Signal Switch states");
            return false;
        }
    }
    state = std::move(candidate);
    for (const auto &endpoint : endpoints)
        state.add(endpoint.id);
    restoring_ = false;
    reconcile();
    refreshUIState();
    notify();
    return true;
}

void Service::reset()
{
    restoring_ = false;
    for (auto &endpoint : endpoints)
    {
        endpoint.desired = false;
        if (endpoint.sound)
            endpoint.sound->stop();
        endpoint.gate->request(false);
    }
    state = {};
    for (const auto &endpoint : endpoints)
        state.add(endpoint.id);
    refreshUIState();
    notify();
}

void Service::beginRestore()
{
    restoring_ = true;
    reconcile();
}
} // namespace WinJACKNexus::SignalSwitch
