#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace switchu::daemon::remote {

constexpr std::size_t kMaxPathLength = 512;
constexpr std::size_t kMaxCommandLength = kMaxPathLength + 16;

enum class CommandKind {
    Invalid,
    InvalidPath,
    Auth,
    Status,
    Launch,
    Press,
};

struct Command {
    CommandKind kind = CommandKind::Invalid;
    std::string argument;
    std::uint32_t holdMs = 0;
    std::uint64_t buttonMask = 0;
};

// Parses one protocol line (without its terminating CR/LF). Launch paths keep
// their spaces; only the command separator is consumed.
Command parseCommand(std::string_view line);

// Accepts an absolute SD .nro path, returning canonical sdmc:/...nro.
// Rejects traversal, control bytes, and paths too large for the loader ABI.
bool normalizeNroPath(std::string_view input, std::string& output);

bool constantTimeEqual(std::string_view left, std::string_view right);

} // namespace switchu::daemon::remote
