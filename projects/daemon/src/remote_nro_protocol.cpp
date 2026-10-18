#include "remote_nro_protocol.hpp"

#include <algorithm>

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
