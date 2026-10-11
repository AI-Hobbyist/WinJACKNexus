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
#include <WinJACKNexus/Meter/MeterCsvWriter.hpp>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

#include <chrono>
#include <memory>
#include <optional>
#include <utility>

namespace WinJACKNexus::Meter
{

namespace
{

constexpr std::size_t maximumQueueRecordCount = 4096;

QString sanitizeDirectoryName(QString value)
{
    value.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1F]")),
                  QStringLiteral("_"));
    value.remove(QRegularExpression(QStringLiteral("[.\\s]+$")));
    return value.isEmpty() ? QStringLiteral("Unnamed") : value;
}

QString quotedCsvField(QString value)
{
    if (!value.contains(u',') && !value.contains(u'"') && !value.contains(u'\n') &&
        !value.contains(u'\r'))
        return value;
    value.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return QStringLiteral("\"") + value + QStringLiteral("\"");
}

QString numericValue(const std::optional<float> &value)
{
    return value.has_value() ? QString::number(*value, 'f', 6) : QString();
}

QString fileNameFor(const MeterHistoryPoint &point)
{
    const auto timestamp =
        point.systemTimeMilliseconds.value_or(QDateTime::currentMSecsSinceEpoch());
    return QDateTime::fromMSecsSinceEpoch(timestamp).toLocalTime().toString(
        QStringLiteral("yyyyMMdd_HHmmss_zzz"));
}

}

struct MeterCsvWriter::Session final
{
    QFile file;
    QString filePath;
};

MeterCsvWriter::MeterCsvWriter() : worker_([this] { workerLoop(); }) {}

MeterCsvWriter::~MeterCsvWriter()
{
    {
        std::lock_guard lock(queueMutex_);
        stopping_ = true;
    }
    queueCondition_.notify_one();
    if (worker_.joinable())
        worker_.join();
}

void MeterCsvWriter::enqueue(Record record)
{
    if (record.instanceId.isEmpty())
        return;

    std::lock_guard lock(queueMutex_);
    if (queue_.size() >= maximumQueueRecordCount)
    {
        auto status = this->status(record.instanceId);
        ++status.droppedRecordCount;
        status.statusKind = MeterCsvStatusKind::queueFull;
        status.statusDetail.clear();
        status.rotationStatus =
            QStringLiteral("CSV 写入队列已满，已丢弃 %1 条记录").arg(status.droppedRecordCount);
        setStatus(record.instanceId, std::move(status));
        return;
    }
    queue_.push_back({QueueEvent::Type::write, std::move(record), {}});
    queueCondition_.notify_one();
}

void MeterCsvWriter::closeSession(const QString &instanceId)
{
    if (instanceId.isEmpty())
        return;

    std::lock_guard lock(queueMutex_);
    queue_.push_back({QueueEvent::Type::close, {}, instanceId});
    queueCondition_.notify_one();
}

MeterCsvWriter::Status MeterCsvWriter::status(const QString &instanceId) const
{
    std::lock_guard lock(statusMutex_);
    const auto iterator = statuses_.find(instanceId);
    return iterator == statuses_.cend() ? Status{} : iterator->second;
}

bool MeterCsvWriter::waitForIdle(int timeoutMilliseconds)
{
    std::unique_lock lock(queueMutex_);
    return idleCondition_.wait_for(lock, std::chrono::milliseconds(timeoutMilliseconds),
                                   [this] { return queue_.empty() && !processing_; });
}

void MeterCsvWriter::workerLoop()
{
    for (;;)
    {
        QueueEvent event;
        {
            std::unique_lock lock(queueMutex_);
            queueCondition_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty() && stopping_)
                break;
            event = std::move(queue_.front());
            queue_.pop_front();
            processing_ = true;
        }

        process(event);

        {
            std::lock_guard lock(queueMutex_);
            processing_ = false;
            if (queue_.empty())
                idleCondition_.notify_all();
        }
    }

    for (auto &[instanceId, session] : sessions_)
    {
        if (session->file.isOpen())
            session->file.close();
        setStatus(instanceId, {{},
                               QStringLiteral("CSV 写入已关闭"),
                               status(instanceId).droppedRecordCount,
                               MeterCsvStatusKind::writerClosed,
                               {}});
    }
    sessions_.clear();
}

void MeterCsvWriter::process(const QueueEvent &event)
{
    if (event.type == QueueEvent::Type::close)
    {
        closeSessionInWorker(event.instanceId, QStringLiteral("CSV 会话已关闭，等待下一条记录"),
                             MeterCsvStatusKind::sessionClosed);
        return;
    }
    writeRecord(event.record);
}

void MeterCsvWriter::writeRecord(const Record &record)
{
    const auto root = record.logRootDirectory.trimmed();
    if (root.isEmpty())
    {
        closeSessionInWorker(record.instanceId, QStringLiteral("CSV 未写入：未设置日志保存根目录"),
                             MeterCsvStatusKind::noDirectory);
        return;
    }

    auto session = sessions_.find(record.instanceId);
    if (session == sessions_.end())
    {
        const auto directory = QDir(root).filePath(sanitizeDirectoryName(record.clientName));
        if (!QDir().mkpath(directory))
        {
            setStatus(record.instanceId,
                      {{},
                       QStringLiteral("CSV 未写入：无法创建目录 %1").arg(directory),
                       status(record.instanceId).droppedRecordCount,
                       MeterCsvStatusKind::directoryFailure,
                       directory});
            return;
        }

        const auto baseName = fileNameFor(record.point);
        QString filePath = QDir(directory).filePath(baseName + QStringLiteral(".csv"));
        for (int suffix = 1; QFileInfo::exists(filePath); ++suffix)
            filePath = QDir(directory).filePath(
                QStringLiteral("%1_%2.csv").arg(baseName).arg(suffix, 3, 10, QLatin1Char('0')));

        auto newSession = std::make_unique<Session>();
        newSession->file.setFileName(filePath);
        if (!newSession->file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            setStatus(record.instanceId,
                      {{},
                       QStringLiteral("CSV 未写入：无法打开文件 %1").arg(filePath),
                       status(record.instanceId).droppedRecordCount,
                       MeterCsvStatusKind::openFailure,
                       filePath});
            return;
        }
        newSession->filePath = filePath;
        newSession->file.write(
            "localTime,elapsedMilliseconds,peakDbfs,rmsDbfs,truePeakDbtp,lfePeakDbfs,momentaryLufs,shortTermLufs,integratedLufs,lraLu\n");
        session = sessions_.emplace(record.instanceId, std::move(newSession)).first;
    }

    const auto timestamp =
        record.point.systemTimeMilliseconds.value_or(QDateTime::currentMSecsSinceEpoch());
    const auto localTime =
        QDateTime::fromMSecsSinceEpoch(timestamp).toLocalTime().toString(Qt::ISODateWithMs);
    const auto &metrics = record.point.metrics;
    const QString row = QStringList{quotedCsvField(localTime),
                                    QString::number(record.point.elapsedMilliseconds),
                                    numericValue(metrics.peakDbfs),
                                    numericValue(metrics.rmsDbfs),
                                    numericValue(metrics.truePeakDbtp),
                                    numericValue(record.point.lfePeakDbfs),
                                    numericValue(metrics.momentaryLufs),
                                    numericValue(metrics.shortTermLufs),
                                    numericValue(metrics.integratedLufs),
                                    numericValue(metrics.lraLu)}
                            .join(u',') +
                        u'\n';
    const auto rowUtf8 = row.toUtf8();
    if (session->second->file.write(rowUtf8) != rowUtf8.size())
    {
        closeSessionInWorker(record.instanceId, QStringLiteral("CSV 写入失败：磁盘可能不可写"),
                             MeterCsvStatusKind::writeFailure);
        return;
    }
    session->second->file.flush();
    const auto previousStatus = status(record.instanceId);
    setStatus(record.instanceId, {session->second->filePath,
                                  QStringLiteral("CSV 正在记录"),
                                  previousStatus.droppedRecordCount,
                                  MeterCsvStatusKind::recording,
                                  {}});
}

void MeterCsvWriter::closeSessionInWorker(const QString &instanceId, const QString &statusText,
                                          MeterCsvStatusKind kind)
{
    const auto session = sessions_.find(instanceId);
    if (session != sessions_.end())
    {
        if (session->second->file.isOpen())
            session->second->file.close();
        sessions_.erase(session);
    }
    const auto previousStatus = status(instanceId);
    setStatus(instanceId, {{}, statusText, previousStatus.droppedRecordCount, kind, {}});
}

void MeterCsvWriter::setStatus(const QString &instanceId, Status status)
{
    std::lock_guard lock(statusMutex_);
    statuses_.insert_or_assign(instanceId, std::move(status));
}

}