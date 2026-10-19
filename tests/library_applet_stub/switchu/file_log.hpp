#pragma once
namespace switchu {
struct FileLog { template<class... T> static void log(const char*, T...) {} };
}
