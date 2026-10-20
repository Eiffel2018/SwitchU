#pragma once

#include <string_view>

namespace switchu::daemon {

enum class BreezeAppletRequest { None, Album, Mii, Remap, NetConnect, UserCreator };

inline BreezeAppletRequest parseBreezeAppletRequest(std::string_view text) {
    if (text.empty() || text.size() > 31 || text.find('\0') != text.npos)
        return BreezeAppletRequest::None;
    const auto end = text.find_first_of(" \r\n");
    const auto word = text.substr(0, end);
    if (end != text.npos && text.substr(end).find_first_not_of(" \r\n") != text.npos)
        return BreezeAppletRequest::None;
    if (word == "album") return BreezeAppletRequest::Album;
    if (word == "mii") return BreezeAppletRequest::Mii;
    if (word == "remap") return BreezeAppletRequest::Remap;
    if (word == "netconnect") return BreezeAppletRequest::NetConnect;
    if (word == "usercreator") return BreezeAppletRequest::UserCreator;
    return BreezeAppletRequest::None;
}

// A system applet gets one return to its Breeze caller. An explicit remote
// replacement or application launch consumes/cancels that return as well.
class BreezeAppletReturn {
public:
    void arm() { pending_ = true; }
    bool take(bool remotePending, bool launchPending) {
        const bool result = pending_ && !remotePending && !launchPending;
        pending_ = false;
        return result;
    }
private:
    bool pending_ = false;
};

} // namespace switchu::daemon
