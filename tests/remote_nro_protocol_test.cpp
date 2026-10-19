#include "../projects/daemon/src/remote_nro_protocol.hpp"

#include <cassert>
#include <string>

using namespace switchu::daemon::remote;

int main() {
    const auto game = parseCommand("gamestart 0100005027a18000");
    assert(game.kind == CommandKind::StartGame && game.titleId == 0x0100005027A18000ULL);
    for (const char* bad : {"gamestart 0000000000000000", "gamestart 0100005027A1800",
         "gamestart 0100005027A180000", "gamestart 0x0100005027A18000",
         "gamestart 0100005027A1800Z", "gamestart 0100005027A18000 extra"})
        assert(parseCommand(bad).kind == CommandKind::Invalid);
    assert(parseCommand("press L+R 100").buttonMask == ((1ULL << 6) | (1ULL << 7)));
    assert(parseCommand("press ZL+ZR+A+B 100").buttonMask == 0x303);
    for (const char* bad : {"press L+L 100", "press A+B+X+Y+L 100", "press HOME+A 100",
         "press L+ 100", "press +R 100", "press L++R 100", "press A+CAPTURE 100"})
        assert(parseCommand(bad).kind == CommandKind::Invalid);
    assert(parseCommand("capture").kind == CommandKind::Capture);
    assert(parseCommand("capture extra").kind == CommandKind::Invalid);
    for (const char* stack : {"0", "1", "2", "3", "4", "6"}) {
        const auto command = parseCommand(std::string("capture ") + stack);
        assert(command.kind == CommandKind::Capture && command.argument == stack);
    }
    for (const char* bad : {"capture 5", "capture 7", "capture -1", "capture 00", "capture 1 extra"})
        assert(parseCommand(bad).kind == CommandKind::Invalid);
    assert(parseCommand("controller off").kind == CommandKind::Controller);
    assert(parseCommand("controller on").argument == "on");
    assert(parseCommand("controller status").argument == "status");
    assert(parseCommand("controller release-all").kind == CommandKind::Invalid);
    {
        const auto key = parseCommand("press PLUS 100");
        assert(key.kind == CommandKind::Press && key.argument == "PLUS");
        assert(key.holdMs == 100 && key.buttonMask == (1ULL << 10));
        assert(parseCommand("press LEFT 1").buttonMask == (1ULL << 12));
        assert(parseCommand("press DOWN 1000").buttonMask == (1ULL << 15));
        assert(parseCommand("press HOME 100").buttonMask == (1ULL << 18));
        assert(parseCommand("press CAPTURE 100").buttonMask == (1ULL << 19));
        assert(parseCommand("press LSL 100").buttonMask == (1ULL << 24));
        assert(parseCommand("press LSR 100").buttonMask == (1ULL << 25));
        assert(parseCommand("press RSL 100").buttonMask == (1ULL << 26));
        assert(parseCommand("press RSR 100").buttonMask == (1ULL << 27));
        for (const char* bad : {"press A 0", "press A 1001", "press A -1",
             "press A 999999999999999999999", "press A 100x", "press A 100 extra",
             "press A ", "press SYNC 100", "press a 100"})
            assert(parseCommand(bad).kind == CommandKind::Invalid);
    }
    {
        const auto stick = parseCommand("stick R -32768 32767 100");
        assert(stick.kind == CommandKind::Stick && stick.argument == "R");
        assert(stick.stickX == -32768 && stick.stickY == 32767 && stick.holdMs == 100);
        assert(parseCommand("stick L 0 0 1").kind == CommandKind::Stick);
        for (const char* bad : {"stick R 32768 0 100", "stick L 0 -32769 100",
             "stick R 0 0 0", "stick L 0 0 1001", "stick R 1 2 3 extra",
             "stick L 0 0", "stick r 0 0 100", "stick R 0.5 0 100",
             "stick R 99999999999999 0 100", "stick R 0  0 100"})
            assert(parseCommand(bad).kind == CommandKind::Invalid);
    }
    {
        const Command command = parseCommand("AuTh 0037");
        assert(command.kind == CommandKind::Auth);
        assert(command.argument == "0037");
        assert(constantTimeEqual(command.argument, "0037"));
        assert(!constantTimeEqual(command.argument, "037"));
    }
    {
        auto tap = parseCommand("touch tap 0 719 100");
        assert(tap.kind == CommandKind::Touch && tap.stickX == 0 && tap.stickY == 719 && tap.holdMs == 100);
        auto twice = parseCommand("touch double 1279 0 80 120");
        assert(twice.kind == CommandKind::Touch && twice.argument == "double" && twice.gapMs == 120);
        assert(parseCommand("touch long 640 360 5000").kind == CommandKind::Touch);
        for (const char* bad : {"touch tap 1280 0 100", "touch tap 0 720 100",
             "touch tap -1 0 100", "touch tap 0 0 0", "touch tap 0 0 301",
             "touch long 0 0 299", "touch long 0 0 5001", "touch double 0 0 100 49",
             "touch double 0 0 100 501", "touch double 0 0 100", "touch tap 0 0 100 100",
             "touch tap 0 0 100 ", "touch tap 0  0 100", "touch double 0 0 100 120 extra",
             "touch swipe 0 0 100", "touch tap 0.5 0 100", "touch tap 99999999999999999 0 100"})
            assert(parseCommand(bad).kind == CommandKind::Invalid);
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
