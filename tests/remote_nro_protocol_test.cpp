#include "../projects/daemon/src/remote_nro_protocol.hpp"

#include <cassert>
#include <string>

using namespace switchu::daemon::remote;

int main() {
    {
        const Command command = parseCommand("AuTh 0037");
        assert(command.kind == CommandKind::Auth);
        assert(command.argument == "0037");
        assert(constantTimeEqual(command.argument, "0037"));
        assert(!constantTimeEqual(command.argument, "037"));
    }
    {
        const Command command = parseCommand("STATUS");
        assert(command.kind == CommandKind::Status);
    }
    {
        const Command command = parseCommand("launch /switch/Breeze Folder/Breeze.nro");
        assert(command.kind == CommandKind::Launch);
        assert(command.argument == "sdmc:/switch/Breeze Folder/Breeze.nro");
    }
    {
        const Command command = parseCommand("launch sdmc:/switch/tools/My App.NRO");
        assert(command.kind == CommandKind::Launch);
        assert(command.argument == "sdmc:/switch/tools/My App.NRO");
    }

    std::string normalized;
    assert(normalizeNroPath("/switch/a b/test.nro", normalized));
    assert(normalized == "sdmc:/switch/a b/test.nro");
    assert(normalizeNroPath("SDMC:/SWITCH/Breeze/Breeze.nro", normalized));
    assert(normalized == "sdmc:/SWITCH/Breeze/Breeze.nro");
    assert(normalizeNroPath("/hbmenu.nro", normalized));
    assert(normalized == "sdmc:/hbmenu.nro");
    const std::string longest = "/" + std::string(501, 'x') + ".nro";
    assert(normalizeNroPath(longest, normalized));
    assert(normalized.size() == 511);
    assert(!normalizeNroPath("/" + std::string(502, 'x') + ".nro", normalized));
    assert(!normalizeNroPath("/switch/quoted\"name.nro", normalized));
    assert(!normalizeNroPath("/switch/../boot.nro", normalized));
    assert(!normalizeNroPath("/switch/a/./test.nro", normalized));
    assert(!normalizeNroPath("/switch//test.nro", normalized));
    assert(!normalizeNroPath("/switch/a/test.nsp", normalized));
    assert(!normalizeNroPath("/switch/a\\test.nro", normalized));
    assert(!normalizeNroPath("/switch/a/../../test.nro", normalized));
    assert(!normalizeNroPath("sdmc:/atmosphere/reboot_payload.bin", normalized));
    assert(!normalizeNroPath("/switch/bad\nname.nro", normalized));
    assert(!normalizeNroPath("/switch/" + std::string(kMaxPathLength, 'x') + ".nro",
                             normalized));

    assert(parseCommand("launch /switch/../x.nro").kind == CommandKind::InvalidPath);
    assert(parseCommand("launch").kind == CommandKind::Invalid);
    assert(parseCommand("auth ").kind == CommandKind::Invalid);
    assert(parseCommand("status\r").kind == CommandKind::Invalid);
    assert(parseCommand(std::string(kMaxCommandLength + 1, 'x')).kind == CommandKind::Invalid);
    return 0;
}
