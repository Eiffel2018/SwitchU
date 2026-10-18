#include "remote_nro_protocol.hpp"

#include <algorithm>
#include <charconv>
#include <array>

namespace switchu::daemon::remote {
namespace {

bool equalAsciiCaseInsensitive(std::string_view left, std::string_view right) {
    if (left.size() != right.size())
        return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        auto lower = [](unsigned char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
        };
        if (lower(static_cast<unsigned char>(left[i]))
            != lower(static_cast<unsigned char>(right[i])))
            return false;
    }
    return true;
}

bool isControl(unsigned char c) {
    return c < 0x20 || c == 0x7f;
}

} // namespace

Command parseCommand(std::string_view line) {
    if (line.empty() || line.size() > kMaxCommandLength)
        return {};
    for (unsigned char c : line) {
        if (isControl(c))
            return {};
    }

    constexpr std::string_view authPrefix = "auth ";
    constexpr std::string_view launchPrefix = "launch ";
    if (line.size() > authPrefix.size()
        && equalAsciiCaseInsensitive(line.substr(0, authPrefix.size()), authPrefix)) {
        std::string_view value = line.substr(authPrefix.size());
        if (value.empty() || value.size() > 64)
            return {};
        return {CommandKind::Auth, std::string(value)};
    }
    if (equalAsciiCaseInsensitive(line, "status"))
        return {CommandKind::Status, {}};
    if (line == "capture")
        return {CommandKind::Capture, {}};
    for (const char* action : {"on", "off", "status"}) {
        if (line == std::string("controller ") + action)
            return {CommandKind::Controller, action};
    }
    constexpr std::string_view stickPrefix = "stick ";
    if (line.substr(0, stickPrefix.size()) == stickPrefix) {
        auto args = line.substr(stickPrefix.size());
        std::array<std::string_view, 4> fields{};
        for (std::size_t i = 0; i < fields.size(); ++i) {
            const auto end = args.find(' ');
            if ((i + 1 == fields.size()) != (end == std::string_view::npos))
                return {};
            fields[i] = args.substr(0, end);
            if (end != std::string_view::npos)
                args.remove_prefix(end + 1);
        }
        if (fields[0] != "L" && fields[0] != "R")
            return {};
        std::int32_t x = 0, y = 0;
        std::uint32_t ms = 0;
        const auto integer = [](std::string_view text, auto& value) {
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
        };
        if (!integer(fields[1], x) || !integer(fields[2], y) || !integer(fields[3], ms)
            || x < -32768 || x > 32767 || y < -32768 || y > 32767 || ms == 0 || ms > 1000)
            return {};
        return {CommandKind::Stick, std::string(fields[0]), ms, 0, x, y};
    }
    constexpr std::string_view pressPrefix = "press ";
    if (line.substr(0, pressPrefix.size()) == pressPrefix) {
        const auto args = line.substr(pressPrefix.size());
        const auto space = args.find(' ');
        if (space == std::string_view::npos)
            return {};
        const auto key = args.substr(0, space);
        const auto duration = args.substr(space + 1);
        std::uint32_t ms = 0;
        const auto parsed = std::from_chars(duration.data(), duration.data() + duration.size(), ms);
        if (parsed.ec != std::errc{} || parsed.ptr != duration.data() + duration.size()
            || ms == 0 || ms > 1000)
            return {};
        constexpr const char* keys[] = {"A", "B", "X", "Y", "LSTICK", "RSTICK",
            "L", "R", "ZL", "ZR", "PLUS", "MINUS", "LEFT", "UP", "RIGHT", "DOWN"};
        for (unsigned i = 0; i < 16; ++i) {
            if (key == keys[i])
                return {CommandKind::Press, std::string(key), ms, 1ULL << i};
        }
        if (key == "HOME")
            return {CommandKind::Press, "HOME", ms, 1ULL << 18};
        if (key == "CAPTURE")
            return {CommandKind::Press, "CAPTURE", ms, 1ULL << 19};
        constexpr const char* rails[] = {"LSL", "LSR", "RSL", "RSR"};
        for (unsigned i = 0; i < 4; ++i) {
            if (key == rails[i])
                return {CommandKind::Press, std::string(key), ms, 1ULL << (24 + i)};
        }
        return {};
    }
    if (line.size() > launchPrefix.size()
        && equalAsciiCaseInsensitive(line.substr(0, launchPrefix.size()), launchPrefix)) {
        std::string path;
        if (!normalizeNroPath(line.substr(launchPrefix.size()), path))
            return {CommandKind::InvalidPath, {}};
        return {CommandKind::Launch, std::move(path)};
    }
    return {};
}

bool normalizeNroPath(std::string_view input, std::string& output) {
    output.clear();
    if (input.empty() || input.size() > kMaxPathLength)
        return false;
    constexpr std::string_view sdmcPrefix = "sdmc:";
    if (input.size() >= sdmcPrefix.size()
        && equalAsciiCaseInsensitive(input.substr(0, sdmcPrefix.size()), sdmcPrefix))
        input.remove_prefix(sdmcPrefix.size());
    for (unsigned char c : input) {
        if (isControl(c) || c == '\\' || c == ':' || c == '"')
            return false;
    }

    if (input.size() < 6 || input.front() != '/' ||
        sdmcPrefix.size() + input.size() >= kMaxPathLength)
        return false;
    // SD-root NROs such as /hbmenu.nro are supported too. Reject duplicate
    // slashes instead of relying on filesystem-specific normalization.

    std::string_view suffix = input.substr(input.size() >= 4 ? input.size() - 4 : 0);
    if (!equalAsciiCaseInsensitive(suffix, ".nro"))
        return false;

    std::size_t componentStart = 1;
    while (componentStart <= input.size()) {
        const std::size_t slash = input.find('/', componentStart);
        const std::size_t end = slash == std::string_view::npos ? input.size() : slash;
        const std::string_view component = input.substr(componentStart, end - componentStart);
        if (component.empty() || component == "." || component == "..")
            return false;
        if (slash == std::string_view::npos)
            break;
        componentStart = slash + 1;
    }

    output.reserve(sdmcPrefix.size() + input.size());
    output.assign(sdmcPrefix);
    output.append(input.data(), input.size());
    return true;
}

bool constantTimeEqual(std::string_view left, std::string_view right) {
    const std::size_t maxLength = std::max(left.size(), right.size());
    unsigned int difference = static_cast<unsigned int>(left.size() ^ right.size());
    for (std::size_t i = 0; i < maxLength; ++i) {
        const unsigned char a = i < left.size() ? static_cast<unsigned char>(left[i]) : 0;
        const unsigned char b = i < right.size() ? static_cast<unsigned char>(right[i]) : 0;
        difference |= static_cast<unsigned int>(a ^ b);
    }
    return difference == 0;
}

} // namespace switchu::daemon::remote
