#include "../projects/daemon/src/breeze_applet_request.hpp"

#include <cassert>
#include <string>

using namespace switchu::daemon;

int main() {
    assert(parseBreezeAppletRequest("album") == BreezeAppletRequest::Album);
    assert(parseBreezeAppletRequest("mii\n") == BreezeAppletRequest::Mii);
    assert(parseBreezeAppletRequest("remap\r\n") == BreezeAppletRequest::Remap);
    assert(parseBreezeAppletRequest("netconnect ") == BreezeAppletRequest::NetConnect);
    assert(parseBreezeAppletRequest("usercreator") == BreezeAppletRequest::UserCreator);
    for (auto text : {"", " album", "Album", "settings", "userpage", "album mii", "mii\t"})
        assert(parseBreezeAppletRequest(text) == BreezeAppletRequest::None);
    assert(parseBreezeAppletRequest(std::string("album\0junk", 10)) == BreezeAppletRequest::None);
    assert(parseBreezeAppletRequest(std::string(32, 'a')) == BreezeAppletRequest::None);

    BreezeAppletReturn back;
    assert(!back.take(false, false));
    back.arm();
    assert(back.take(false, false));
    assert(!back.take(false, false));
    back.arm();
    assert(!back.take(true, false)); // explicit NRO replacement wins
    assert(!back.take(false, false)); // no stale return when that NRO exits
    back.arm();
    assert(!back.take(false, true)); // a requested NSP launch also wins
    assert(!back.take(false, false));
}
