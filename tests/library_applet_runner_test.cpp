#include "../projects/daemon/src/library_applet_runner.hpp"
#include <cassert>
#include <iostream>

static int creates, starts, joins, closes, foreground, terminates;
static Result startResult;
static bool active;
static LibAppletMode createdMode;
Result appletCreateLibraryApplet(AppletHolder*, AppletId, LibAppletMode mode) { ++creates; createdMode=mode; return 0; }
void appletHolderClose(AppletHolder*) { ++closes; }
Result appletHolderStart(AppletHolder*) { ++starts; return startResult; }
bool appletHolderActive(AppletHolder*) { return active; }
bool appletHolderCheckFinished(AppletHolder*) { return false; }
Result appletHolderTerminate(AppletHolder*) { ++terminates; active=false; return 0; }
Result appletHolderRequestExitOrTerminate(AppletHolder*, u64) { active=false; return 0; }
void appletHolderJoin(AppletHolder*) { ++joins; }
LibAppletExitReason appletHolderGetExitReason(AppletHolder*) { return LibAppletExitReason_Normal; }
Result appletRequestToGetForeground() { ++foreground; return 0; }
void libappletArgsCreate(LibAppletArgs*, std::uint32_t) {}
void libappletArgsSetPlayStartupSound(LibAppletArgs*, bool) {}
Result libappletArgsPush(LibAppletArgs*, AppletHolder*) { return 0; }
Result libappletPushInData(AppletHolder*, const void*, std::size_t) { return 0; }
Result libappletPopOutData(AppletHolder*, void*, std::size_t, std::size_t*) { return 0; }
void svcSleepThread(std::int64_t) {}
static bool yes() { return true; }
static void reset() { creates=starts=joins=closes=foreground=terminates=0; startResult=0; active=false; }

int main() {
    using namespace switchu::daemon;
    LibraryAppletRequest request{};
    request.id=AppletId_LibraryAppletPhotoViewer;
    reset();
    assert(runLibraryApplet(request,nullptr,nullptr)==0);
    assert(createdMode==LibAppletMode_AllForeground && foreground==1 && joins==1 && closes==1);

    // A normal background exit cannot reclaim qlaunch foreground from a game.
    reset(); request.mode=LibAppletMode_Background;
    assert(runLibraryApplet(request,nullptr,nullptr)==0);
    assert(createdMode==LibAppletMode_Background && foreground==0 && joins==1 && closes==1);

    // Failed/terminated background startup must also leave foreground alone.
    reset(); startResult=9;
    assert(runLibraryApplet(request,nullptr,nullptr)==9);
    assert(foreground==0 && joins==0 && closes==1);
    reset(); active=true; request.terminateOnExit=yes;
    AppletHolder* holder=nullptr; request.activeHolder=&holder;
    assert(runLibraryApplet(request,nullptr,yes)==0);
    assert(terminates==1 && joins==1 && closes==1 && foreground==0 && holder==nullptr);

    // Existing foreground callers retain their explicit skip-restore contract.
    reset(); request.mode=LibAppletMode_AllForeground; request.skipForegroundRestore=yes;
    assert(runLibraryApplet(request,nullptr,nullptr)==0 && foreground==0);
    reset(); request.id=AppletId_None;
    assert(runLibraryApplet(request,nullptr,nullptr)!=0 && creates==0 && closes==0);
    std::cout << "Foreground/background runner, failure, termination and cleanup checks passed\n";
}
