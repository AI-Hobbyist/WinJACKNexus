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
#include <WinJACKNexus/Audio/JackRingBuffer.hpp>

namespace WinJACKNexus::Audio {

JackRingBuffer::JackRingBuffer(std::size_t size) noexcept
{
    auto& api = JackApi::instance();
    if (api.load())
        ringbuffer_ = api.ringbufferCreate(size);
}

JackRingBuffer::~JackRingBuffer() noexcept
{
    release();
}

JackRingBuffer::JackRingBuffer(JackRingBuffer&& other) noexcept
    : ringbuffer_(other.ringbuffer_)
{
    other.ringbuffer_ = nullptr;
}

JackRingBuffer& JackRingBuffer::operator=(JackRingBuffer&& other) noexcept
{
    if (this == &other)
        return *this;
    release();
    ringbuffer_ = other.ringbuffer_;
    other.ringbuffer_ = nullptr;
    return *this;
}

bool JackRingBuffer::isValid() const noexcept
{
    return ringbuffer_ != nullptr;
}

std::size_t JackRingBuffer::read(char* destination, std::size_t count) noexcept
{
    return ringbuffer_ != nullptr && destination != nullptr
        ? JackApi::instance().ringbufferRead(ringbuffer_, destination, count)
        : 0;
}

std::size_t JackRingBuffer::peek(char* destination, std::size_t count) noexcept
{
    return ringbuffer_ != nullptr && destination != nullptr
        ? JackApi::instance().ringbufferPeek(ringbuffer_, destination, count)
        : 0;
}

std::size_t JackRingBuffer::write(const char* source, std::size_t count) noexcept
{
    return ringbuffer_ != nullptr && source != nullptr
        ? JackApi::instance().ringbufferWrite(ringbuffer_, source, count)
        : 0;
}

bool JackRingBuffer::getReadSegments(JackRingBufferSegment segments[2]) const noexcept
{
    if (segments == nullptr)
        return false;
    segments[0] = {};
    segments[1] = {};
    if (ringbuffer_ == nullptr)
        return false;

    jack_ringbuffer_data_t vectors[2] {};
    JackApi::instance().ringbufferGetReadVector(ringbuffer_, vectors);
    segments[0] = { vectors[0].buf, vectors[0].len };
    segments[1] = { vectors[1].buf, vectors[1].len };
    return segments[0].size != 0 || segments[1].size != 0;
}

bool JackRingBuffer::getWriteSegments(JackRingBufferSegment segments[2]) const noexcept
{
    if (segments == nullptr)
        return false;
    segments[0] = {};
    segments[1] = {};
    if (ringbuffer_ == nullptr)
        return false;

    jack_ringbuffer_data_t vectors[2] {};
    JackApi::instance().ringbufferGetWriteVector(ringbuffer_, vectors);
    segments[0] = { vectors[0].buf, vectors[0].len };
    segments[1] = { vectors[1].buf, vectors[1].len };
    return segments[0].size != 0 || segments[1].size != 0;
}

void JackRingBuffer::advanceRead(std::size_t count) noexcept
{
    if (ringbuffer_ != nullptr)
        JackApi::instance().ringbufferReadAdvance(ringbuffer_, count);
}

void JackRingBuffer::advanceWrite(std::size_t count) noexcept
{
    if (ringbuffer_ != nullptr)
        JackApi::instance().ringbufferWriteAdvance(ringbuffer_, count);
}

std::size_t JackRingBuffer::readableBytes() const noexcept
{
    return ringbuffer_ != nullptr ? JackApi::instance().ringbufferReadSpace(ringbuffer_) : 0;
}

std::size_t JackRingBuffer::writableBytes() const noexcept
{
    return ringbuffer_ != nullptr ? JackApi::instance().ringbufferWriteSpace(ringbuffer_) : 0;
}

bool JackRingBuffer::lockMemory() noexcept
{
    return ringbuffer_ != nullptr && JackApi::instance().ringbufferMlock(ringbuffer_) == 0;
}

void JackRingBuffer::reset() noexcept
{
    if (ringbuffer_ != nullptr)
        JackApi::instance().ringbufferReset(ringbuffer_);
}

jack_ringbuffer_t* JackRingBuffer::nativeHandle() const noexcept
{
    return ringbuffer_;
}

void JackRingBuffer::release() noexcept
{
    if (ringbuffer_ != nullptr) {
        JackApi::instance().ringbufferFree(ringbuffer_);
        ringbuffer_ = nullptr;
    }
}

}