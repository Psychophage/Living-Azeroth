// SPDX-License-Identifier: GPL-2.0-or-later
#include "bc_frames.h"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace BotControl
{
namespace
{
std::optional<uint32_t> Number(std::string_view text)
{
    uint32_t value = 0;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size() || text.empty())
        return std::nullopt;
    return value;
}

std::size_t Digits(uint32_t value)
{
    return std::to_string(value).size();
}
}

std::optional<Frame> ParseFrame(std::string_view text)
{
    if (text.size() < 6 || text.size() > MaxFrame || text[0] != FrameVersion)
        return std::nullopt;
    auto idEnd = text.find(':', 1);
    auto slash = text.find('/', idEnd == std::string_view::npos ? 0 : idEnd);
    auto chunkStart = text.find(':', slash == std::string_view::npos ? 0 : slash);
    if (idEnd == std::string_view::npos || slash == std::string_view::npos || chunkStart == std::string_view::npos)
        return std::nullopt;
    Frame frame;
    frame.id = std::string(text.substr(1, idEnd - 1));
    if (frame.id.empty() || frame.id.size() > 6 ||
        !std::all_of(frame.id.begin(), frame.id.end(), [](unsigned char c) { return std::isalnum(c); }))
        return std::nullopt;
    auto part = Number(text.substr(idEnd + 1, slash - idEnd - 1));
    auto total = Number(text.substr(slash + 1, chunkStart - slash - 1));
    if (!part || !total || *part < 1 || *total < 1 || *part > *total || *total > MaxParts)
        return std::nullopt;
    frame.part = *part;
    frame.total = *total;
    frame.chunk = std::string(text.substr(chunkStart + 1));
    return frame;
}

std::vector<std::string> EncodeFrames(std::string_view id, std::string_view message)
{
    // The header grows with the number of parts, so settle the count first.
    uint32_t total = 1;
    std::size_t room = 0;
    for (;;)
    {
        std::size_t header = 1 + id.size() + 1 + Digits(total) * 2 + 2;
        room = MaxFrame - header;
        uint32_t needed = std::max<uint32_t>(1, uint32_t((message.size() + room - 1) / room));
        if (needed <= total)
            break;
        total = needed;
    }
    std::vector<std::string> frames;
    for (uint32_t part = 1; part <= total; ++part)
    {
        std::string frame(1, FrameVersion);
        frame.append(id);
        frame += ':' + std::to_string(part) + '/' + std::to_string(total) + ':';
        std::size_t offset = std::size_t(part - 1) * room;
        if (offset < message.size())
            frame.append(message.substr(offset, room));
        frames.push_back(std::move(frame));
    }
    return frames;
}

Assembler::Result Assembler::Add(Frame const& frame, std::string& message)
{
    if (frame.part == 1)
    {
        _id = frame.id;
        _total = frame.total;
        _next = 1;
        _buffer.clear();
    }
    if (frame.id != _id || frame.total != _total || frame.part != _next)
    {
        _id.clear();
        _buffer.clear();
        return Result::Rejected;
    }
    if (_buffer.size() + frame.chunk.size() > MaxMessage)
    {
        _id.clear();
        _buffer.clear();
        return Result::Rejected;
    }
    _buffer += frame.chunk;
    if (++_next <= _total)
        return Result::Partial;
    message = std::move(_buffer);
    _buffer.clear();
    _id.clear();
    return Result::Complete;
}
}
