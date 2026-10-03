/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinJACKNexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#pragma once

#include <WinJACKNexus/Meter/MeterViewSnapshot.hpp>

#include <QString>

#include <cstddef>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace WinJACKNexus::Meter {

class MeterCsvWriter final {
public:
    struct Record final {
        QString instanceId;
        QString clientName;
        QString logRootDirectory;
        MeterHistoryPoint point;
    };

    struct Status final {
        QString activeFilePath;
        QString rotationStatus;
        std::size_t droppedRecordCount {};
        MeterCsvStatusKind statusKind = MeterCsvStatusKind::unspecified;
        QString statusDetail;
    };

    MeterCsvWriter();
    ~MeterCsvWriter();

    MeterCsvWriter(const MeterCsvWriter&) = delete;
    MeterCsvWriter& operator=(const MeterCsvWriter&) = delete;

    void enqueue(Record record);
    void closeSession(const QString& instanceId);
    Status status(const QString& instanceId) const;
    bool waitForIdle(int timeoutMilliseconds);

private:
    struct QueueEvent final {
        enum class Type {
            write,
            close
        };

        Type type = Type::write;
        Record record;
        QString instanceId;
    };

    struct Session;

    void workerLoop();
    void process(const QueueEvent& event);
    void writeRecord(const Record& record);
    void closeSessionInWorker(const QString& instanceId, const QString& status, MeterCsvStatusKind kind);
    void setStatus(const QString& instanceId, Status status);

    mutable std::mutex queueMutex_;
    std::condition_variable queueCondition_;
    std::condition_variable idleCondition_;
    std::deque<QueueEvent> queue_;
    bool stopping_ = false;
    bool processing_ = false;

    mutable std::mutex statusMutex_;
    std::map<QString, Status> statuses_;
    std::map<QString, std::unique_ptr<Session>> sessions_;
    std::thread worker_;
};

}