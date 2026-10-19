#pragma once
#include <cstddef>
#include <cstdint>
using Result = std::uint32_t;
using u64 = std::uint64_t;
enum AppletId { AppletId_None, AppletId_LibraryAppletPhotoViewer };
enum LibAppletMode { LibAppletMode_AllForeground, LibAppletMode_Background };
enum LibAppletExitReason { LibAppletExitReason_Normal, LibAppletExitReason_Canceled, LibAppletExitReason_Unexpected };
struct AppletHolder {};
struct LibAppletArgs {};
#define R_FAILED(x) ((x) != 0)
#define R_SUCCEEDED(x) ((x) == 0)
#define MAKERESULT(m,d) ((Result)(((m) << 9) | (d)))
#define R_MODULE(x) ((x) & 511)
#define R_DESCRIPTION(x) ((x) >> 9)
constexpr int Module_Libnx = 345;
constexpr int LibnxError_BadInput = 1;
constexpr int LibnxError_LibAppletBadExit = 2;
Result appletCreateLibraryApplet(AppletHolder*, AppletId, LibAppletMode);
void appletHolderClose(AppletHolder*);
Result appletHolderStart(AppletHolder*);
bool appletHolderActive(AppletHolder*);
bool appletHolderCheckFinished(AppletHolder*);
Result appletHolderTerminate(AppletHolder*);
Result appletHolderRequestExitOrTerminate(AppletHolder*, u64);
void appletHolderJoin(AppletHolder*);
LibAppletExitReason appletHolderGetExitReason(AppletHolder*);
Result appletRequestToGetForeground();
void libappletArgsCreate(LibAppletArgs*, std::uint32_t);
void libappletArgsSetPlayStartupSound(LibAppletArgs*, bool);
Result libappletArgsPush(LibAppletArgs*, AppletHolder*);
Result libappletPushInData(AppletHolder*, const void*, std::size_t);
Result libappletPopOutData(AppletHolder*, void*, std::size_t, std::size_t*);
void svcSleepThread(std::int64_t);
