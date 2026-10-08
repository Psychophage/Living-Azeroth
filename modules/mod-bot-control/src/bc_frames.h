// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef BC_FRAMES_H
#define BC_FRAMES_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace BotControl
{
// An addon whisper's text is "LABC\t<frame>" and at most 255 bytes.
inline constexpr std::string_view Prefix = "LABC";
inline constexpr char FrameVersion = '1';
inline constexpr std::size_t MaxFrame = 255 - 5;
inline constexpr std::size_t MaxMessage = 4096;
inline constexpr uint32_t MaxParts = 24;

// "<version><id>:<part>/<total>:<chunk>": id is 1-6 letters or digits, parts count from 1.
// A message is JSON split across as many frames as it needs.
struct Frame
{
    std::string id;
    uint32_t part = 0;
    uint32_t total = 0;
    std::string chunk;
};

std::optional<Frame> ParseFrame(std::string_view text);
std::vector<std::string> EncodeFrames(std::string_view id, std::string_view message);

// Joins one sender's frames. Frames arrive in order; a new id abandons an unfinished message.
class Assembler
{
public:
    enum class Result
    {
        Partial,
        Complete,
        Rejected,
    };

    Result Add(Frame const& frame, std::string& message);

private:
    std::string _id;
    uint32_t _total = 0;
    uint32_t _next = 1;
    std::string _buffer;
};
}

#endif
