
#include <switch.h>
#include <switch/applets/friends_la.h>
#include <cstdlib>
#include <switchu/smi_protocol.hpp>
#include <switchu/smi_helpers.hpp>
#include <switchu/control_cache.hpp>
#include <switchu/ns_ext.hpp>
#include <switchu/file_log.hpp>
#include <switchu/sd_commit.hpp>
#include "app_manager.hpp"
#include "breeze_launch_handoff.hpp"
#include "breeze_applet_request.hpp"
#include "ecs.hpp"
#include "library_applet_runner.hpp"
#include "menu_launcher.hpp"
#include "system_action_queue.hpp"
#include "remote_nro_server.hpp"
#include "../breeze_loader/nro_launch_input.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <utility>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

using namespace switchu;

static bool g_timeReady = false;
static bool g_setsysReady = false;
static bool g_setReady = false;
static bool g_nsReady = false;
static bool g_ldrShellReady = false;
static bool g_accountReady = false;
static bool g_nssuReady = false;
static bool g_psmReady = false;
static bool g_lblReady = false;
static bool g_hidReady = false;

extern "C" {
    u32 __nx_applet_type = AppletType_SystemApplet;
    u32 __nx_fs_num_sessions = 3;

    size_t __nx_heap_size = 0x800000;
    TimeServiceType __nx_time_service_type = TimeServiceType_System;
}

extern "C" void __appInit(void) {
    Result rc;

    svcOutputDebugString("[SwitchU-daemon] __appInit start", 34);
    daemon::initializeExternalContentAllocator();

    rc = smInitialize();
    if (R_FAILED(rc)) {
        svcOutputDebugString("[SwitchU-daemon] smInitialize FAIL", 35);
        diagAbortWithResult(MAKERESULT(Module_Libnx, 500));
    }

    rc = fsInitialize();
    if (R_FAILED(rc)) {
        svcOutputDebugString("[SwitchU-daemon] fsInitialize FAIL", 35);
        diagAbortWithResult(MAKERESULT(Module_Libnx, 501));
    }

    rc = appletInitialize();
    if (R_FAILED(rc)) {
        svcOutputDebugString("[SwitchU-daemon] appletInitialize FAIL", 39);
        diagAbortWithResult(MAKERESULT(Module_Libnx, 502));
    }
    svcOutputDebugString("[SwitchU-daemon] appletInitialize OK", 37);

    rc = timeInitialize();
    g_timeReady = R_SUCCEEDED(rc);
    if (R_FAILED(rc))
        svcOutputDebugString("[SwitchU-daemon] timeInitialize FAIL", 37);

    rc = setsysInitialize();
    g_setsysReady = R_SUCCEEDED(rc);
    if (R_FAILED(rc))
        svcOutputDebugString("[SwitchU-daemon] setsysInitialize FAIL", 39);

    rc = setInitialize();
    g_setReady = R_SUCCEEDED(rc);
    if (R_FAILED(rc))
        svcOutputDebugString("[SwitchU-daemon] setInitialize FAIL", 36);

    if (g_setsysReady) {
        SetSysFirmwareVersion fw = {};
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro) | BIT(31));
    }

    rc = nsInitialize();
    if (R_FAILED(rc)) {
        svcOutputDebugString("[SwitchU-daemon] nsInitialize FAIL", 35);
        diagAbortWithResult(rc);
    }
    g_nsReady = true;

    rc = ldrShellInitialize();
    if (R_FAILED(rc)) {
        svcOutputDebugString("[SwitchU-daemon] ldrShellInitialize FAIL", 41);
        diagAbortWithResult(rc);
    }
    g_ldrShellReady = true;

    rc = accountInitialize(AccountServiceType_System);
    if (R_FAILED(rc)) {
        svcOutputDebugString("[SwitchU-daemon] accountInitialize FAIL", 40);
        diagAbortWithResult(rc);
    }
    g_accountReady = true;

    rc = nssuInitialize();
    g_nssuReady = R_SUCCEEDED(rc);
    if (R_FAILED(rc))
        svcOutputDebugString("[SwitchU-daemon] nssuInitialize FAIL", 37);

    rc = psmInitialize();
    g_psmReady = R_SUCCEEDED(rc);
    if (R_FAILED(rc))
        svcOutputDebugString("[SwitchU-daemon] psmInitialize FAIL", 36);

    rc = lblInitialize();
    g_lblReady = R_SUCCEEDED(rc);
    if (R_FAILED(rc))
        svcOutputDebugString("[SwitchU-daemon] lblInitialize FAIL", 36);

    rc = hidInitialize();
    g_hidReady = R_SUCCEEDED(rc);
    if (R_FAILED(rc))
        svcOutputDebugString("[SwitchU-daemon] hidInitialize FAIL", 36);

    rc = fsdevMountSdmc();
    if (R_FAILED(rc)) {
        svcOutputDebugString("[SwitchU-daemon] fsdevMountSdmc FAIL", 37);
        svcSleepThread(100'000'000ULL);
        rc = fsdevMountSdmc();
    }

    switchu::FileLog::open("daemon");
    switchu::FileLog::log("[daemon] __appInit complete (sd mount: 0x%X)", rc);
    switchu::FileLog::log("[daemon] BreezePC recovery v2: probe=2s loader-timeout=10s foreground-confirmation=required");
    switchu::FileLog::log("[daemon] services time=%d setsys=%d set=%d ns=%d ldr=%d account=%d nssu=%d psm=%d lbl=%d hid=%d",
                          g_timeReady ? 1 : 0,
                          g_setsysReady ? 1 : 0,
                          g_setReady ? 1 : 0,
                          g_nsReady ? 1 : 0,
                          g_ldrShellReady ? 1 : 0,
                          g_accountReady ? 1 : 0,
                          g_nssuReady ? 1 : 0,
                          g_psmReady ? 1 : 0,
                          g_lblReady ? 1 : 0,
                          g_hidReady ? 1 : 0);

    svcOutputDebugString("[SwitchU-daemon] __appInit done", 31);
}

extern "C" void __appExit(void) {
    switchu::FileLog::log("[daemon] __appExit");
    switchu::FileLog::close();

    if (g_hidReady) hidExit();
    if (g_lblReady) lblExit();
    if (g_psmReady) psmExit();
    if (g_nssuReady) nssuExit();
    if (g_accountReady) accountExit();
    if (g_ldrShellReady) ldrShellExit();
    if (g_nsReady) nsExit();
    if (g_setReady) setExit();
    if (g_setsysReady) setsysExit();
    if (g_timeReady) timeExit();

    appletExit();
    fsdevUnmountAll();
    fsExit();
    smExit();
}

static std::atomic<bool> g_running{true};
static std::atomic<bool> g_powerSequenceStarted{false};
static UEvent g_mainWakeEvent{};
static daemon::remote::Request g_remoteNroRequest{};
static bool g_remoteNroPending = false;
static bool g_remoteNroForegroundRequested = false;
static bool g_daemonForegroundKnown = true;
static uint64_t g_remoteNroReceivedAt = 0;
static uint64_t g_remoteNroActiveId = 0;
static uint64_t g_remoteNroLoaderStartedAt = 0;
static uint64_t g_remoteNroLastProbeAt = 0;
static bool g_remoteNroAcked = false;
static Result g_remoteNroAckResult = 0;
static bool g_remoteNroTimedOut = false;
static bool g_remoteNroHidden = false; // remote NRO held behind its suspended game
static void pollRemoteNroRequests();
static bool processRemoteNroRequest();
static UEvent g_controlCacheWakeEvent{};
static Event g_generalChannelEvent{};
static bool g_generalChannelEventReady = false;
static std::atomic<bool> g_eventRefreshPending{false};
static std::atomic<bool> g_eventGcMountFailure{false};
static std::atomic<bool> g_batteryRefreshPending{true};
static std::atomic<Result> g_eventGcMountRc{0};
static bool g_initialEventSkipped = false;
static int  g_eventPollCountdown  = 0;
static int  g_eventPollsRemaining = 0;
// Polling application views touches enough NS state to stutter the menu. Keep
// the settling window, but spread a small number of probes over twelve seconds
// instead of hammering it fifty times in ten seconds.
static constexpr int kViewPollIntervalTicks = 200;
static constexpr int kViewPollAttempts = 6;
static int  g_menuRelaunchCooldown = 0;
static int  g_menuFastExitCount = 0;
static s32      g_lastRecordCount = 0;
static uint64_t g_lastRecordTids[1024] = {};
static uint32_t g_lastViewFlags[1024]  = {};

static constexpr s32 kMaxTrackedApplicationRecords = 1024;
static constexpr s32 kApplicationRecordChunkCount = 30;

struct DaemonAppCatalogEntry {
    uint64_t titleId = 0;
    uint32_t viewFlags = 0;
    bool startupUserKnown = false;
    uint8_t startupUserAccount = 1;
    uint8_t startupUserAccountOption = 0;
    std::string name;
};

static std::vector<DaemonAppCatalogEntry> g_appCatalog;
static std::vector<uint64_t> g_lastCatalogOrder;   // title ids as last written
static std::atomic<bool> g_appCatalogRefreshPending{false};
// A catalogue rebuild can complete while a full-application homebrew owns the
// foreground and the menu process is absent. Preserve that edge so the freshly
// relaunched menu receives one coalesced refresh instead of keeping a stale
// catalogue until the daemon is restarted.
static std::atomic<bool> g_catalogChangedWhileMenuAway{false};
static uint64_t g_catalogHoldStartTick = 0;
static uint64_t g_catalogHoldNs = 0;
static constexpr uint64_t kCatalogHoldAfterHomeNs = 2'000'000'000ULL;
static constexpr uint64_t kCatalogHoldAfterMenuLaunchNs = 800'000'000ULL;
static constexpr uint64_t kCatalogHoldAfterMenuReadyNs = 200'000'000ULL;
static constexpr const char* kAppCatalogPath = "sdmc:/config/SwitchU/applist.bin";
static constexpr const char* kAppCatalogTmpPath = "sdmc:/config/SwitchU/applist.tmp";
static constexpr const char* kAppCatalogBackupPath = "sdmc:/config/SwitchU/applist.bak";
static std::mutex g_controlCacheQueueMutex;
// A title that fails to cache goes back on the queue rather than waiting for the
// next boot: the failures seen in practice are transient SD write errors during
// a large rebuild. The attempt count is what stops a title that always fails
// from cycling forever.
struct QueuedControlCacheTitle {
    uint64_t titleId = 0;
    int      attempts = 0;
};
static std::vector<QueuedControlCacheTitle> g_controlCacheQueue;
static constexpr int kControlCacheMaxAttempts = 3;
static std::atomic<bool> g_controlCacheRefreshPending{false};
static std::atomic<uint64_t> g_controlCacheHoldStartTick{0};
static constexpr uint64_t kControlCacheCoalesceNs = 600'000'000ULL;

static daemon::SystemActionQueue g_actionQueue;
static smi::OperationOutcome g_lastOperationFailure{};
static bool g_foregroundAppletActive = false;
static bool g_pendingForegroundAppletHome = false;
static bool g_pendingHomeMenuLaunch = false;
static const char* g_pendingHomeMenuSource = nullptr;
static uint64_t g_pendingHomeMenuStartedAt = 0;
static uint64_t g_pendingBreezeReturnSessionId = 0;
// AppletMessage 50: a program called appletRequestLaunchApplication(). The
// launch waits until the menu / foreground applet in front has closed.
static bool g_launchRequestPending = false;
// Whether that request came from an applet in front (hbmenu / sphaira / DBI /
// Breeze) rather than from the running application. Decided when the request
// arrives: by the time it is acted on, the application has been stopped.
static bool g_launchRequestFromApplet = false;
// Capture before Breeze exits and its session/flag is cleared.
static bool g_launchRequestFromBreeze = false;
static daemon::BreezeLaunchHandoff g_breezeLaunchHandoff;
static bool g_breezeReturnWaitLogged = false;
static uint64_t g_breezeRestoreTargetSessionId = 0;
static uint64_t g_breezeReturnAttemptSessionId = 0;
static uint64_t g_breezeReturnAttemptStartedAt = 0;
static uint64_t g_breezeReturnLastProbeAt = 0;
static bool g_breezeReturnAttemptTimedOut = false;
static void retryBreezeReturn(uint64_t sessionId, const char* reason);
static AccountUid g_lastLaunchUid{};

static void cancelBreezeReturn(const char* reason) {
    if (g_breezeLaunchHandoff.pending() || g_pendingBreezeReturnSessionId != 0)
        switchu::FileLog::log("[breeze-return] cancelled: %s", reason);
    g_breezeLaunchHandoff.cancel();
    g_pendingBreezeReturnSessionId = 0;
    g_breezeRestoreTargetSessionId = 0;
}

// Breeze Home toggle (fork-only): see smi::kBreezeHomeToggleFlag.
enum class BreezeToggleMode : uint8_t { Off, Keep, Restart, Overlay };
static bool g_breezeCandidate = false;       // Album / User Page applet that may be Breeze
static bool g_breezeSession = false;         // that applet announced itself as Breeze
static bool g_breezeDirect = false;          // the slot boots Breeze itself (User Page or
                                             // Album loader), so the daemon can relaunch it
static const char* g_breezeSlotName = "Album";
static daemon::BreezeAppletReturn g_breezeAppletReturn;
static AccountUid g_breezeUid{};
static bool g_breezeHidden = false;          // keep: game in front, Breeze held behind it
static bool g_breezeResumeOnClose = false;   // restart: resume the game once Breeze closed
static bool g_breezeCloseForGame = false;    // held Breeze closing so the game can open an applet
static bool g_breezeCloseForSleep = false;   // held Breeze closing before the console sleeps
static bool g_breezeRelaunchPending = false; // next HOME from the game reopens Breeze
static uint64_t g_breezePollTick = 0;
static uint64_t g_breezeMissingSince = 0;

// Breeze overlay (see smi::BreezeOverlayCommand): the game keeps the foreground
// and Breeze draws over it on its own layer.
enum class BreezeOverlayState : uint8_t { None, Hidden, Shown };
static BreezeOverlayState g_breezeOverlay = BreezeOverlayState::None;
static bool g_breezeOverlayCapable = false;         // breeze_running holds the capability
static bool g_breezeOverlayForegroundRequested = false;
static AppletHolder* g_breezeHolder = nullptr;       // set by the runner while the applet runs
static bool g_breezeStartedBackground = false;
static uint64_t g_breezeBackgroundFailedSessionId = 0;

static uint8_t g_lastBatteryPercent = 0xFF;
static PsmChargerType g_lastChargerType = (PsmChargerType)0xFF;

static bool shouldDeferViewPolling() {
    return daemon::app::isRunning() &&
           daemon::app::hasForeground() &&
           !daemon::menu_la::isActive() &&
           !g_foregroundAppletActive;
}

static uint64_t elapsedNsSince(uint64_t startTick) {
    return armTicksToNs(armGetSystemTick() - startTick);
}

static uint64_t catalogHoldRemainingNs() {
    if (g_catalogHoldStartTick == 0)
        return 0;
    const uint64_t elapsed = elapsedNsSince(g_catalogHoldStartTick);
    return elapsed >= g_catalogHoldNs ? 0 : g_catalogHoldNs - elapsed;
}

static void armCatalogHold(uint64_t holdNs) {
    g_catalogHoldStartTick = armGetSystemTick();
    g_catalogHoldNs = holdNs;
}

static void shortenCatalogHold(uint64_t holdNs) {
    if (catalogHoldRemainingNs() > holdNs)
        armCatalogHold(holdNs);
}

static void clearCatalogHold() {
    g_catalogHoldStartTick = 0;
    g_catalogHoldNs = 0;
}

static bool controlCacheHoldActive() {
    const uint64_t startTick = g_controlCacheHoldStartTick.load();
    return startTick != 0 && elapsedNsSince(startTick) < kControlCacheCoalesceNs;
}

static bool listApplicationRecords(std::vector<switchu::ns::ExtApplicationRecord>& records,
                                   const char* tag) {
    records.clear();

    switchu::ns::ExtApplicationRecord chunk[kApplicationRecordChunkCount] = {};
    s32 offset = 0;
    while (offset < kMaxTrackedApplicationRecords) {
        s32 readCount = 0;
        Result rc = nsListApplicationRecord(
            reinterpret_cast<NsApplicationRecord*>(chunk),
            kApplicationRecordChunkCount,
            offset,
            &readCount);
        if (R_FAILED(rc)) {
            switchu::FileLog::log("[%s] nsListApplicationRecord FAIL: 0x%X offset=%d",
                                  tag, rc, offset);
            records.clear();
            return false;
        }
        if (readCount <= 0)
            break;

        const s32 remaining = kMaxTrackedApplicationRecords - offset;
        const s32 appendCount = readCount > remaining ? remaining : readCount;
        records.insert(records.end(), chunk, chunk + appendCount);
        offset += readCount;

        if (readCount < kApplicationRecordChunkCount)
            break;
    }

    std::sort(records.begin(), records.end(),
              [](const switchu::ns::ExtApplicationRecord& a,
                 const switchu::ns::ExtApplicationRecord& b) {
                  return a.id < b.id;
              });
    return true;
}

static bool queryApplicationViews(const std::vector<switchu::ns::ExtApplicationRecord>& records,
                                  std::vector<switchu::ns::ExtApplicationView>& views,
                                  const char* tag) {
    views.clear();
    if (records.empty())
        return true;

    std::vector<uint64_t> tids(records.size());
    for (size_t i = 0; i < records.size(); ++i)
        tids[i] = records[i].id;

    views.resize(records.size());
    Result rc = switchu::ns::queryApplicationViews(
        tids.data(),
        static_cast<int>(tids.size()),
        views.data());
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[%s] queryApplicationViews FAIL: 0x%X", tag, rc);
        std::fill(views.begin(), views.end(), switchu::ns::ExtApplicationView{});
        return false;
    }

    return true;
}

static void enqueueControlCacheRecords(const std::vector<switchu::ns::ExtApplicationRecord>& records) {
    bool queued = false;
    std::lock_guard<std::mutex> lock(g_controlCacheQueueMutex);
    for (const auto& record : records) {
        if (record.id == 0 || switchu::control_cache::hasUsableCache(record.id))
            continue;
        const auto found = std::find_if(
            g_controlCacheQueue.begin(), g_controlCacheQueue.end(),
            [&record](const QueuedControlCacheTitle& entry) {
                return entry.titleId == record.id;
            });
        if (found == g_controlCacheQueue.end()) {
            g_controlCacheQueue.push_back({record.id, 0});
            queued = true;
        }
    }
    if (queued)
        ueventSignal(&g_controlCacheWakeEvent);
}

static bool popControlCacheTitle(uint64_t& outTitleId, int& outAttempts) {
    std::lock_guard<std::mutex> lock(g_controlCacheQueueMutex);
    if (g_controlCacheQueue.empty())
        return false;

    outTitleId = g_controlCacheQueue.front().titleId;
    outAttempts = g_controlCacheQueue.front().attempts;
    g_controlCacheQueue.erase(g_controlCacheQueue.begin());
    return true;
}

// Requeued at the back, so every other title gets its turn first and a retry
// never spins on the same failing read.
static void requeueControlCacheTitle(uint64_t titleId, int attempts) {
    if (attempts + 1 >= kControlCacheMaxAttempts) {
        switchu::FileLog::log("[control-cache] giving up on 0x%016lX after %d attempts",
                              titleId, attempts + 1);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_controlCacheQueueMutex);
        g_controlCacheQueue.push_back({titleId, attempts + 1});
    }
    switchu::FileLog::log("[control-cache] requeued 0x%016lX attempt %d",
                          titleId, attempts + 2);
    ueventSignal(&g_controlCacheWakeEvent);
}

static bool writeAppCatalogFile() {
    std::error_code fsEc;
    std::filesystem::create_directory("sdmc:/config", fsEc);
    fsEc.clear();
    std::filesystem::create_directory("sdmc:/config/SwitchU", fsEc);

    std::ofstream f(kAppCatalogTmpPath, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        switchu::FileLog::log("[catalog] fopen tmp FAIL");
        return false;
    }

    uint32_t count = static_cast<uint32_t>(g_appCatalog.size());
    bool ok = static_cast<bool>(f.write(reinterpret_cast<const char*>(&count), sizeof(count)));
    for (const auto& ent : g_appCatalog) {
        if (!ok) break;

        smi::AppEntryHeader eh{};
        eh.title_id = ent.titleId;
        eh.name_len = static_cast<uint32_t>(ent.name.size());
        eh.icon_data_len = 0;
        eh.view_flags = ent.viewFlags;
        eh.startup_user_account = ent.startupUserAccount;
        eh.startup_user_account_option = ent.startupUserAccountOption;
        eh.startup_user_known = ent.startupUserKnown ? 1 : 0;

        ok = static_cast<bool>(f.write(reinterpret_cast<const char*>(&eh), sizeof(eh)));
        if (ok && eh.name_len > 0)
            ok = static_cast<bool>(f.write(ent.name.data(), static_cast<std::streamsize>(eh.name_len)));
    }

    f.close();
    ok = ok && static_cast<bool>(f);
    if (!ok) {
        fsEc.clear();
        std::filesystem::remove(kAppCatalogTmpPath, fsEc);
        switchu::FileLog::log("[catalog] write FAIL");
        return false;
    }

    // FAT does not guarantee replace-existing rename semantics. Keep the last
    // complete catalog until the new file is in place so a power loss never
    // leaves the menu with only a truncated/absent catalog.
    fsEc.clear();
    std::filesystem::remove(kAppCatalogBackupPath, fsEc);
    fsEc.clear();
    if (std::filesystem::exists(kAppCatalogPath, fsEc)) {
        fsEc.clear();
        std::filesystem::rename(kAppCatalogPath, kAppCatalogBackupPath, fsEc);
        if (fsEc) {
            std::filesystem::remove(kAppCatalogTmpPath, fsEc);
            switchu::FileLog::log("[catalog] backup rename FAIL");
            return false;
        }
    }

    fsEc.clear();
    std::filesystem::rename(kAppCatalogTmpPath, kAppCatalogPath, fsEc);
    if (fsEc) {
        std::error_code restoreEc;
        if (std::filesystem::exists(kAppCatalogBackupPath, restoreEc)) {
            restoreEc.clear();
            std::filesystem::rename(kAppCatalogBackupPath, kAppCatalogPath, restoreEc);
        }
        fsEc.clear();
        std::filesystem::remove(kAppCatalogTmpPath, fsEc);
        switchu::FileLog::log("[catalog] commit rename FAIL restore=%d",
                              restoreEc ? 0 : 1);
        return false;
    }

    fsEc.clear();
    std::filesystem::remove(kAppCatalogBackupPath, fsEc);
    return true;
}

static bool rebuildAppCatalog(const char* reason, bool* outChanged = nullptr) {
    std::vector<switchu::ns::ExtApplicationRecord> records;
    if (!listApplicationRecords(records, "catalog"))
        return false;
    enqueueControlCacheRecords(records);

    std::vector<switchu::ns::ExtApplicationView> views;
    queryApplicationViews(records, views, "catalog");

    const s32 count = static_cast<s32>(records.size());
    g_appCatalog.clear();
    g_appCatalog.reserve(count);

    // The catalogue is written newest first, which is the menu's "Recent"
    // order. ns bumps a record's last_updated when the title is installed,
    // updated or launched (nsTouchApplication), which is the order Nintendo's
    // HOME shows, so a fresh install comes first as well.
    std::vector<s32> order(static_cast<std::size_t>(count));
    for (s32 i = 0; i < count; ++i)
        order[static_cast<std::size_t>(i)] = i;
    std::stable_sort(order.begin(), order.end(), [&records](s32 a, s32 b) {
        return records[a].last_updated > records[b].last_updated;
    });
    for (s32 i = 0; i < count && i < 3; ++i) {
        const auto& record = records[order[static_cast<std::size_t>(i)]];
        switchu::FileLog::log("[catalog] recent #%d 0x%016lX last_updated=%lu event=%u",
                              i + 1, static_cast<unsigned long>(record.id),
                              static_cast<unsigned long>(record.last_updated),
                              static_cast<unsigned>(record.last_event));
    }

    // Resolve display name and startup-user policy here, on the daemon, so the
    // menu can build its grid straight from applist.bin. Otherwise every menu
    // cold start reopens one .meta file per installed title.
    for (const s32 i : order) {
        const uint64_t tid = records[i].id;
        DaemonAppCatalogEntry ent;
        ent.titleId = tid;
        ent.viewFlags = views[i].flags;
        char fallbackName[17] = {};
        std::snprintf(fallbackName, sizeof(fallbackName), "%016lX",
                      static_cast<unsigned long>(tid));
        ent.name = fallbackName;

        switchu::control_cache::Meta meta{};
        if (switchu::control_cache::readMeta(tid, meta)) {
            const std::size_t nameLength = strnlen(meta.name, sizeof(meta.name));
            if (nameLength > 0 &&
                switchu::control_cache::isValidUtf8(meta.name, nameLength + 1)) {
                ent.name.assign(meta.name, nameLength);
            }
            ent.startupUserAccount = meta.startup_user_account;
            ent.startupUserAccountOption = meta.startup_user_account_option;
            ent.startupUserKnown = true;
        }
        g_appCatalog.push_back(std::move(ent));
    }

    const s32 prevCount = g_lastRecordCount;
    bool changed = prevCount != count;
    if (!changed) {
        for (s32 i = 0; i < count; ++i) {
            if (g_lastRecordTids[i] != records[i].id) {
                changed = true;
                break;
            }
        }
    }
    // A launch only reorders the catalogue, but the menu's "Recent" view
    // still has to hear about it.
    std::vector<uint64_t> recentOrder;
    recentOrder.reserve(g_appCatalog.size());
    for (const auto& ent : g_appCatalog)
        recentOrder.push_back(ent.titleId);
    if (recentOrder != g_lastCatalogOrder) {
        changed = true;
        g_lastCatalogOrder = std::move(recentOrder);
    }
    g_lastRecordCount = count;
    for (s32 i = 0; i < count; ++i) {
        g_lastRecordTids[i] = records[i].id;
        g_lastViewFlags[i] = views[i].flags;
    }
    for (s32 i = count; i < prevCount && i < kMaxTrackedApplicationRecords; ++i) {
        g_lastRecordTids[i] = 0;
        g_lastViewFlags[i] = 0;
    }

    const bool ok = writeAppCatalogFile();
    switchu::FileLog::log("[catalog] rebuilt %d/%d apps reason=%s write=%d",
                          (int)g_appCatalog.size(), (int)count, reason, ok ? 1 : 0);
    g_appCatalog.clear();
    g_appCatalog.shrink_to_fit();
    if (outChanged)
        *outChanged = changed;
    return ok;
}

// A launch moves the title to the front of the catalogue. ns may not signal a
// record update for nsTouchApplication, so ask for the rebuild here; it waits
// until the game leaves the foreground like any other.
static void noteApplicationLaunched() {
    g_appCatalogRefreshPending.store(true);
}

static void cancelViewPolling(const char* reason) {
    const bool hadPendingEvent = g_eventRefreshPending.exchange(false);
    if (hadPendingEvent || g_eventPollsRemaining > 0) {
        switchu::FileLog::log("[views] cancelling background poll (%s) pending=%d remaining=%d",
                              reason,
                              hadPendingEvent ? 1 : 0,
                              g_eventPollsRemaining);
    }
    g_eventPollCountdown = 0;
    g_eventPollsRemaining = 0;
}

static smi::SystemStatus buildSystemStatus() {
    smi::SystemStatus st{};
    st.suspended_app_id = daemon::app::suspendedTitleId();
    st.app_running = daemon::app::isRunning();
    st.last_failure = g_lastOperationFailure;
    return st;
}

static void recordOperationResult(uint64_t requestId,
                                  smi::SystemMessage command,
                                  Result result,
                                  uint64_t titleId = 0) {
    if (R_SUCCEEDED(result)) {
        g_lastOperationFailure = {};
        return;
    }

    g_lastOperationFailure = {
        .request_id = requestId,
        .title_id = titleId,
        .command = static_cast<uint32_t>(command),
        .result = static_cast<uint32_t>(result),
    };
    switchu::FileLog::log(
        "[operation] request=%lu command=%u title=0x%016lX FAIL=0x%X",
        static_cast<unsigned long>(requestId),
        static_cast<unsigned>(command), titleId, result);
}

struct ScopedService {
    Service value{};

    ScopedService() = default;
    ScopedService(const ScopedService&) = delete;
    ScopedService& operator=(const ScopedService&) = delete;

    ~ScopedService() {
        serviceClose(&value);
    }
};

static Result openTimeAdminService(ScopedService& out) {
    const Result rc = smGetService(&out.value, "time:a");
    switchu::FileLog::log(
        "[settings-time] daemon open time:a rc=0x%X", rc);
    return rc;
}

static Result setLiveAutomaticCorrection(bool enabled) {
    ScopedService admin;
    Result rc = openTimeAdminService(admin);
    if (R_SUCCEEDED(rc)) {
        const u8 flag = enabled ? 1 : 0;
        rc = serviceDispatchIn(&admin.value, 101, flag);
    }

    switchu::FileLog::log(
        "[settings-time] daemon live automaticCorrection enabled=%d rc=0x%X",
        enabled ? 1 : 0, rc);
    return rc;
}

static Result setInternetTimeSync(bool enabled) {
    const Result setsysRc =
        setsysSetUserSystemClockAutomaticCorrectionEnabled(enabled);
    const Result liveRc = R_SUCCEEDED(setsysRc)
        ? setLiveAutomaticCorrection(enabled)
        : setsysRc;

    bool confirmed = !enabled;
    const Result confirmRc =
        setsysIsUserSystemClockAutomaticCorrectionEnabled(&confirmed);
    Result result = 0;
    if (R_FAILED(setsysRc)) {
        result = setsysRc;
    } else if (R_FAILED(liveRc)) {
        result = liveRc;
    } else if (R_FAILED(confirmRc)) {
        result = confirmRc;
    } else if (confirmed != enabled) {
        result = MAKERESULT(Module_Libnx, 908);
    }

    switchu::FileLog::log(
        "[settings-time] daemon internetTimeSync enabled=%d setsys=0x%X live=0x%X confirm=0x%X state=%d result=0x%X",
        enabled ? 1 : 0, setsysRc, liveRc, confirmRc,
        confirmed ? 1 : 0, result);
    return result;
}

static Result setManualDateTime(const smi::ManualDateTimeArgs& args) {
    TimeCalendarTime calendar{};
    calendar.year = static_cast<u16>(args.year);
    calendar.month = static_cast<u8>(args.month);
    calendar.day = static_cast<u8>(args.day);
    calendar.hour = static_cast<u8>(args.hour);
    calendar.minute = static_cast<u8>(args.minute);
    calendar.second = 0;

    u64 timestamps[2]{};
    s32 timestampCount = 0;
    Result rc = timeToPosixTimeWithMyRule(&calendar, timestamps, 2, &timestampCount);
    switchu::FileLog::log(
        "[settings-time] daemon convert %04u-%02u-%02u %02u:%02u rc=0x%X count=%d posix=%llu",
        args.year, args.month, args.day, args.hour, args.minute,
        rc, timestampCount,
        (unsigned long long)(timestampCount > 0 ? timestamps[0] : 0));
    if (R_SUCCEEDED(rc) && timestampCount <= 0)
        rc = MAKERESULT(Module_Libnx, 902);
    if (R_FAILED(rc))
        return rc;
    const u64 targetTimestamp = timestamps[0];

    auto isCloseToTarget = [&](u64 actual) -> bool {
        const u64 delta = actual > targetTimestamp ? actual - targetTimestamp
                                                   : targetTimestamp - actual;
        return delta <= 120;
    };

    auto waitForUserClock = [&]() -> Result {
        Result lastRc = 0;
        u64 last = 0;
        for (int attempt = 0; attempt < 20; ++attempt) {
            lastRc = timeGetCurrentTime(TimeType_UserSystemClock, &last);
            const bool close = R_SUCCEEDED(lastRc) && isCloseToTarget(last);
            if (close)
                return 0;
            svcSleepThread(100'000'000ULL);
        }
        switchu::FileLog::log(
            "[settings-time] daemon user clock verification failed rc=0x%X posix=%llu",
            lastRc, (unsigned long long)last);
        return R_FAILED(lastRc) ? lastRc : MAKERESULT(Module_Libnx, 906);
    };

    const Result enableRc = setLiveAutomaticCorrection(true);
    Result networkRc = enableRc;
    Result waitRc = enableRc;
    if (R_SUCCEEDED(enableRc)) {
        networkRc = timeSetCurrentTime(
            TimeType_NetworkSystemClock, targetTimestamp);
        switchu::FileLog::log(
            "[settings-time] daemon temporary automaticCorrection network set rc=0x%X",
            networkRc);
        if (R_SUCCEEDED(networkRc))
            waitRc = waitForUserClock();
    }

    const Result restoreRc = setLiveAutomaticCorrection(false);
    switchu::FileLog::log(
        "[settings-time] daemon temporary automaticCorrection enable=0x%X network=0x%X wait=0x%X restore=0x%X",
        enableRc, networkRc, waitRc, restoreRc);

    if (R_FAILED(enableRc))
        return enableRc;
    if (R_FAILED(networkRc))
        return networkRc;
    if (R_FAILED(waitRc))
        return waitRc;
    return restoreRc;
}

static void pushNotification(smi::MenuMessage msg,
                             uint64_t app_id = 0,
                             uint32_t payload = 0) {
    if (!daemon::menu_la::isActive()) return;
    smi::DaemonNotification notif{};
    notif.magic   = smi::kNotifyMagic;
    notif.msg     = msg;
    notif.app_id  = app_id;
    notif.payload = payload;
    AppletStorage st;
    if (R_SUCCEEDED(appletCreateStorage(&st, sizeof(notif)))) {
        Result rc = appletStorageWrite(&st, 0, &notif, sizeof(notif));
        if (R_SUCCEEDED(rc)) {
            switchu::FileLog::log("[notify] push msg=%u", (unsigned)msg);
            daemon::menu_la::pushStorage(&st);
        } else {
            appletStorageClose(&st);
            switchu::FileLog::log("[notify] write FAIL: 0x%X msg=%u", rc, (unsigned)msg);
        }
    } else {
        switchu::FileLog::log("[notify] push FAIL (alloc) msg=%u", (unsigned)msg);
    }
}

static bool queryBatteryStatus(uint8_t& percent, PsmChargerType& chargerType) {
    if (!g_psmReady)
        return false;

    u32 charge = 100;
    Result chargeRc = psmGetBatteryChargePercentage(&charge);
    if (R_FAILED(chargeRc)) {
        switchu::FileLog::log("[battery] psmGetBatteryChargePercentage FAIL: 0x%X", chargeRc);
        return false;
    }

    PsmChargerType ct = PsmChargerType_Unconnected;
    Result chargerRc = psmGetChargerType(&ct);
    if (R_FAILED(chargerRc)) {
        switchu::FileLog::log("[battery] psmGetChargerType FAIL: 0x%X", chargerRc);
        ct = PsmChargerType_Unconnected;
    }

    if (charge > 100)
        charge = 100;
    percent = static_cast<uint8_t>(charge);
    chargerType = ct;
    return true;
}

static void pushBatteryStatusNotification(bool force) {
    if (!daemon::menu_la::isActive())
        return;

    uint8_t percent = 0;
    PsmChargerType chargerType = PsmChargerType_Unconnected;
    if (!queryBatteryStatus(percent, chargerType))
        return;

    if (!force && percent == g_lastBatteryPercent && chargerType == g_lastChargerType)
        return;

    g_lastBatteryPercent = percent;
    g_lastChargerType = chargerType;

    const uint32_t payload = smi::makeBatteryPayload(percent, static_cast<uint32_t>(chargerType));
    switchu::FileLog::log("[battery] notify percent=%u charger=%u",
                          (unsigned)percent,
                          (unsigned)chargerType);
    pushNotification(smi::MenuMessage::BatteryStatusChanged, 0, payload);
}

static void logHomeState(const char* source, const char* stage) {
    switchu::FileLog::log("[%s] HOME %s: appRunning=%d appFg=%d suspended=0x%016lX menuHolder=%d menuActive=%d fgApplet=%d",
                          source, stage,
                          daemon::app::isRunning() ? 1 : 0,
                          daemon::app::hasForeground() ? 1 : 0,
                          daemon::app::suspendedTitleId(),
                          daemon::menu_la::hasHolder() ? 1 : 0,
                          daemon::menu_la::isActive() ? 1 : 0,
                          g_foregroundAppletActive ? 1 : 0);
}

static bool takeForegroundFromRunningApp(const char* source) {
    if (!daemon::app::isRunning() || !daemon::app::hasForeground())
        return true;

    const uint64_t startedAt = armGetSystemTick();
    Result fgRc = appletRequestToGetForeground();
    switchu::FileLog::log("[%s] RequestToGetForeground rc=0x%X elapsed=%lums",
                          source, fgRc,
                          static_cast<unsigned long>(
                              armTicksToNs(armGetSystemTick() - startedAt) / 1'000'000ULL));
    if (R_FAILED(fgRc))
        return false;

    daemon::app::onHomeSuspend();
    return true;
}

// Breeze opts in through smi::kBreezeHomeToggleFlag and announces itself with
// smi::kBreezeRunningFlag when it starts inside the Album (hbmenu) or User Page
// (profile takeover) applet. While it runs, HOME toggles between Breeze and the
// suspended game instead of reopening the menu.
static bool breezeFlagExists(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file)
        return false;
    fclose(file);
    return true;
}

static BreezeToggleMode readBreezeToggleMode() {
    FILE* file = fopen(smi::kBreezeHomeToggleFlag, "rb");
    if (!file)
        return BreezeToggleMode::Off;
    char value[16] = {};
    fread(value, 1, sizeof(value) - 1, file);
    fclose(file);
    if (std::strncmp(value, "restart", 7) == 0)
        return BreezeToggleMode::Restart;
    if (std::strncmp(value, "keep", 4) == 0)
        return BreezeToggleMode::Keep;
    if (std::strncmp(value, "overlay", 7) == 0)
        return BreezeToggleMode::Overlay;
    return BreezeToggleMode::Off;
}

// Fast restart relaunches the slot Breeze came from. Only the User Page loader
// and the fork's Album loader boot Breeze directly; from hbmenu it would land in
// hbmenu, so Breeze is kept alive there instead.
static BreezeToggleMode effectiveBreezeMode() {
    const BreezeToggleMode mode = readBreezeToggleMode();
    if (mode == BreezeToggleMode::Restart && !g_breezeDirect)
        return BreezeToggleMode::Keep;
    // An older Breeze can't draw an overlay; hold it behind the game instead.
    if (mode == BreezeToggleMode::Overlay && !g_breezeOverlayCapable)
        return BreezeToggleMode::Keep;
    return mode;
}

// Sleep and wake, kept out of daemon.log: that file is buffered, held open and
// lost if the console hangs, and its archives are hard to tell apart.
static void powerTrace(const char* fmt, ...) {
    char line[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    switchu::FileLog::log("[power] %s", line);

    std::error_code ec;
    const auto size = std::filesystem::file_size(smi::kPowerLogPath, ec);
    if (!ec && size > 64 * 1024)
        ::remove(smi::kPowerLogPath);
    if (FILE* file = fopen(smi::kPowerLogPath, "a")) {
        std::fprintf(file, "%lums %s\n",
                     static_cast<unsigned long>(armTicksToNs(armGetSystemTick()) / 1'000'000ULL), line);
        fclose(file);
    }
    fsdevCommitDevice("sdmc");
}

// Breeze records the overlay layers it creates and deletes the file once it has
// destroyed them. Anything left means Breeze was terminated with a layer alive.
static void destroyLeftoverOverlayLayers(const char* when) {
    FILE* file = fopen(smi::kBreezeOverlayLayersPath, "r");
    if (!file)
        return;
    std::vector<unsigned long long> ids;
    unsigned long long id = 0;
    while (fscanf(file, "%llu", &id) == 1)
        ids.push_back(id);
    fclose(file);
    ::remove(smi::kBreezeOverlayLayersPath);
    if (ids.empty())
        return;

    Service root{}, app{}, manager{};
    Result rc = smGetService(&root, "vi:m");
    if (R_SUCCEEDED(rc)) {
        const u32 inval = 1;
        rc = serviceDispatchIn(&root, 2, inval, .out_num_objects = 1, .out_objects = &app);
    }
    if (R_SUCCEEDED(rc))
        rc = serviceDispatch(&app, 102, .out_num_objects = 1, .out_objects = &manager);
    for (unsigned long long layer : ids) {
        const Result destroyRc = R_SUCCEEDED(rc) ? serviceDispatchIn(&manager, 2011, layer) : rc;
        powerTrace("%s: leftover Breeze overlay layer %llu destroy rc=0x%X", when, layer, destroyRc);
    }
    if (serviceIsActive(&manager)) serviceClose(&manager);
    if (serviceIsActive(&app)) serviceClose(&app);
    if (serviceIsActive(&root)) serviceClose(&root);
}

static void breezeOverlayTrace(const char* fmt, ...) {
    char line[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    switchu::FileLog::log("[breeze] %s", line);
}

// breeze_running holds space-separated capabilities.
static bool readBreezeCapability(const char* capability) {
    FILE* file = fopen(smi::kBreezeRunningFlag, "rb");
    if (!file)
        return false;
    char value[64] = {};
    fread(value, 1, sizeof(value) - 1, file);
    fclose(file);
    const size_t length = std::strlen(capability);
    for (const char* token = value; *token != '\0';) {
        while (*token == ' ' || *token == '\n' || *token == '\r')
            ++token;
        const char* end = token;
        while (*end != '\0' && *end != ' ' && *end != '\n' && *end != '\r')
            ++end;
        if (static_cast<size_t>(end - token) == length && std::strncmp(token, capability, length) == 0)
            return true;
        token = end;
    }
    return false;
}

static bool readBreezeOverlayCapability() {
    return readBreezeCapability(smi::kBreezeOverlayCapability);
}

static const char* breezeOverlayCommandName(smi::BreezeOverlayCommand command) {
    switch (command) {
        case smi::BreezeOverlayCommand::EnterHidden: return "EnterHidden";
        case smi::BreezeOverlayCommand::Show: return "Show";
        case smi::BreezeOverlayCommand::Hide: return "Hide";
        case smi::BreezeOverlayCommand::EnterNormal: return "EnterNormal";
        case smi::BreezeOverlayCommand::Release: return "Release";
        case smi::BreezeOverlayCommand::Ack: return "Ack";
        case smi::BreezeOverlayCommand::RequestForeground: return "RequestForeground";
        case smi::BreezeOverlayCommand::StateChanged: return "StateChanged";
    }
    return "?";
}

// Pops everything Breeze sent. Returns true if it included the Ack for `awaited`.
static bool drainBreezeOverlayMessages(smi::BreezeOverlayCommand awaited) {
    bool acked = false;
    AppletStorage storage{};
    while (g_breezeHolder && R_SUCCEEDED(appletHolderPopInteractiveOutData(g_breezeHolder, &storage))) {
        smi::BreezeOverlayMessage message{};
        message.magic = 0;
        const Result readRc = appletStorageRead(&storage, 0, &message, sizeof(message));
        appletStorageClose(&storage);
        if (R_FAILED(readRc) || message.magic != smi::kBreezeOverlayMagic)
            continue;
        const auto command = static_cast<smi::BreezeOverlayCommand>(message.command);
        if (command == smi::BreezeOverlayCommand::Ack) {
            if (message.arg == static_cast<uint32_t>(awaited))
                acked = true;
        } else if (command == smi::BreezeOverlayCommand::RequestForeground) {
            g_breezeOverlayForegroundRequested = true;
        } else if (command == smi::BreezeOverlayCommand::StateChanged &&
                   g_breezeOverlay != BreezeOverlayState::None) {
            // Breeze showed itself on a breakpoint hit, or hid itself after one.
            if (message.arg == static_cast<uint32_t>(smi::BreezeOverlayCommand::Show))
                g_breezeOverlay = BreezeOverlayState::Shown;
            else if (message.arg == static_cast<uint32_t>(smi::BreezeOverlayCommand::Hide))
                g_breezeOverlay = BreezeOverlayState::Hidden;
            breezeOverlayTrace("overlay state changed by Breeze: %d", static_cast<int>(g_breezeOverlay));
        }
    }
    return acked;
}

// Sends one command and waits for Breeze's Ack.
static bool sendBreezeOverlay(smi::BreezeOverlayCommand command, uint64_t timeoutMs) {
    if (!g_breezeHolder) {
        breezeOverlayTrace("overlay %s: no applet holder", breezeOverlayCommandName(command));
        return false;
    }
    smi::BreezeOverlayMessage message{};
    message.command = static_cast<uint32_t>(command);
    AppletStorage storage{};
    Result rc = appletCreateStorage(&storage, sizeof(message));
    if (R_SUCCEEDED(rc)) {
        rc = appletStorageWrite(&storage, 0, &message, sizeof(message));
        if (R_SUCCEEDED(rc))
            rc = appletHolderPushInteractiveInData(g_breezeHolder, &storage);
        appletStorageClose(&storage);
    }
    if (R_FAILED(rc)) {
        breezeOverlayTrace("overlay %s push FAIL: 0x%X holder=%d",
                           breezeOverlayCommandName(command), rc, g_breezeHolder ? 1 : 0);
        return false;
    }

    const uint64_t startedAt = armGetSystemTick();
    for (;;) {
        const uint64_t elapsedMs = armTicksToNs(armGetSystemTick() - startedAt) / 1'000'000ULL;
        if (drainBreezeOverlayMessages(command)) {
            breezeOverlayTrace("overlay %s acked after %lums",
                               breezeOverlayCommandName(command),
                               static_cast<unsigned long>(elapsedMs));
            return true;
        }
        if (elapsedMs >= timeoutMs)
            break;
        svcSleepThread(10'000'000ULL);
    }
    breezeOverlayTrace("overlay %s: no ack within %lums",
                       breezeOverlayCommandName(command), static_cast<unsigned long>(timeoutMs));
    return false;
}

// hid:sys EnableAppletToGetInput for every applet and the game. Breeze gives
// input back itself on Hide; this covers a Breeze closed while its overlay held
// the controller (crash or terminate), which would leave the game without input.
static void restoreInputAfterBreezeOverlay() {
    Service hidsys{};
    Result rc = smGetService(&hidsys, "hid:sys");
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[breeze] overlay input restore: hid:sys FAIL 0x%X", rc);
        return;
    }
    const auto enable = [&hidsys](u64 aruid) {
        const struct {
            u8 permit;
            u64 aruid;
        } in = {1, aruid};
        return serviceDispatchIn(&hidsys, 503, in);
    };
    if (R_SUCCEEDED(pmdmntInitialize())) {
        for (u64 programId = 0x0100000000001000ULL; programId < 0x0100000000001020ULL; ++programId) {
            u64 pid = 0;
            if (R_SUCCEEDED(pmdmntGetProcessId(&pid, programId)) && pid != 0)
                enable(pid);
        }
        u64 appPid = 0;
        if (R_SUCCEEDED(pmdmntGetApplicationProcessId(&appPid)) && appPid != 0) {
            rc = enable(appPid);
            switchu::FileLog::log("[breeze] overlay input restored for game pid=%lu rc=0x%X",
                                  static_cast<unsigned long>(appPid), rc);
        }
        pmdmntExit();
    }
    serviceClose(&hidsys);
}

static void beginBreezeCandidate(bool direct, AccountUid uid, const char* slotName) {
    ::remove(smi::kBreezeRunningFlag);
    ::remove(smi::kBreezeOpenMenuFlag);
    ::remove(smi::kBreezeOpenAppletFlag);
    g_breezeCandidate = true;
    g_breezeSession = false;
    g_breezeDirect = direct;
    g_breezeSlotName = slotName;
    g_breezeUid = uid;
    g_breezeHidden = false;
    g_breezeResumeOnClose = false;
    g_breezeCloseForGame = false;
    g_breezeCloseForSleep = false;
    g_breezeRelaunchPending = false;
    g_breezePollTick = 0;
    g_breezeMissingSince = 0;
    g_breezeOverlay = BreezeOverlayState::None;
    g_breezeOverlayCapable = false;
    g_breezeOverlayForegroundRequested = false;
    g_breezeStartedBackground = false;
}

static bool breezeSkipForegroundRestore() {
    return (g_remoteNroPending && g_remoteNroRequest.kind == daemon::remote::RequestKind::StartGame)
        || g_breezeResumeOnClose || g_breezeCloseForGame;
}

static bool remoteNroSkipForegroundRestore() {
    if (g_breezeSession)
        return breezeSkipForegroundRestore();
    // A generic remote NRO may close while its game is back in front. Do not
    // steal foreground from that game as the library-applet runner unwinds.
    return g_remoteNroHidden && daemon::app::isRunning() && daemon::app::hasForeground();
}

// Breeze held behind the game can't answer an exit request, so asking it to
// exit only waited out the timeout (about 3 s before sleep). Close it at once.
static bool breezeTerminateOnExit() {
    return g_breezeReturnAttemptTimedOut || g_breezeCloseForSleep || g_breezeCloseForGame ||
           (g_launchRequestPending && g_breezeHidden);
}

// Before closing a held Breeze. A Breeze with kBreezeOverlayCapability2 gets
// Release: it lets a game stopped at one of its breakpoints run again (nothing
// would resume it once Breeze is gone) and drops its layer. An older Breeze only
// needs Hide, and only when it holds the controller.
static void releaseBreezeOverlayBeforeClose() {
    if (g_breezeOverlay == BreezeOverlayState::None)
        return;
    if (readBreezeCapability(smi::kBreezeOverlayCapability2)) {
        if (sendBreezeOverlay(smi::BreezeOverlayCommand::Release, 1000))
            g_breezeOverlay = BreezeOverlayState::Hidden;
    } else if (g_breezeOverlay == BreezeOverlayState::Shown &&
               sendBreezeOverlay(smi::BreezeOverlayCommand::Hide, 500)) {
        g_breezeOverlay = BreezeOverlayState::Hidden;
    }
}

// A library applet held behind the game blocks the game's own library applets
// (keyboard, error dialogs). Close Breeze as soon as the game asks for one.
static void closeHeldBreezeIfGameNeedsApplet(const char* trigger) {
    if (!g_breezeHidden || g_breezeCloseForGame || g_breezeCloseForSleep || !daemon::app::isRunning())
        return;
    bool appletsLeft = false;
    const Result rc = daemon::app::areLibraryAppletsLeft(&appletsLeft);
    if (R_FAILED(rc) || !appletsLeft)
        return;
    switchu::FileLog::log("[breeze] game opened a library applet (%s); closing held Breeze",
                          trigger);
    releaseBreezeOverlayBeforeClose();
    g_breezeCloseForGame = true;
    g_pendingForegroundAppletHome = true;
}

// Overlay mode HOME. Returns true when HOME was consumed.
static bool handleBreezeOverlayHome(const char* source) {
    switch (g_breezeOverlay) {
        case BreezeOverlayState::Shown:
            // A Breeze that doesn't answer may still hold the controller; take
            // it back for the game so HOME always returns control.
            if (!sendBreezeOverlay(smi::BreezeOverlayCommand::Hide, 1000))
                restoreInputAfterBreezeOverlay();
            g_breezeOverlay = BreezeOverlayState::Hidden;
            breezeOverlayTrace("%s HOME -> hide Breeze overlay", source);
            return true;

        case BreezeOverlayState::Hidden:
            if (sendBreezeOverlay(smi::BreezeOverlayCommand::Show, 1000))
                g_breezeOverlay = BreezeOverlayState::Shown;
            breezeOverlayTrace("%s HOME -> show Breeze overlay state=%d", source,
                               static_cast<int>(g_breezeOverlay));
            return true;

        case BreezeOverlayState::None:
            break;
    }

    // Breeze in front of the suspended game. It must stop drawing to its applet
    // window before the game takes the foreground, or its render loop waits
    // for that window forever.
    if (daemon::app::hasForeground())
        return false;
    // Breeze's Exit removes breeze_running; hbmenu left in the applet can't answer.
    if (!readBreezeOverlayCapability()) {
        breezeOverlayTrace("%s HOME: breeze_running has no capability; not using the overlay", source);
        return false;
    }
    if (!sendBreezeOverlay(smi::BreezeOverlayCommand::EnterHidden, 1500)) {
        breezeOverlayTrace("%s HOME -> Breeze overlay unavailable; holding Breeze instead", source);
        return false;
    }
    const Result rc = daemon::app::resume();
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[%s] HOME -> game resume FAIL: 0x%X; Breeze stays in front", source, rc);
        sendBreezeOverlay(smi::BreezeOverlayCommand::EnterNormal, 1000);
        return true;
    }
    g_breezeHidden = true;
    g_breezeOverlay = BreezeOverlayState::Hidden;
    breezeOverlayTrace("%s HOME -> game, Breeze overlay ready", source);
    return true;
}

// Returns true when HOME was consumed by the Breeze toggle.
static bool handleBreezeHome(const char* source) {
    // Breeze's SwitchU button: close the applet (hbmenu, after Breeze exited) so
    // the SwitchU menu opens, the same way HOME does with the toggle Off.
    const bool openMenuRequested = breezeFlagExists(smi::kBreezeOpenMenuFlag);
    if (openMenuRequested)
        ::remove(smi::kBreezeOpenMenuFlag);
    if (openMenuRequested && g_breezeSession && g_foregroundAppletActive &&
        !g_breezeCloseForGame && !g_breezeCloseForSleep) {
        if (g_breezeOverlay != BreezeOverlayState::None) {
            sendBreezeOverlay(smi::BreezeOverlayCommand::EnterNormal, 1000);
            g_breezeOverlay = BreezeOverlayState::None;
        }
        if (daemon::app::isRunning() && daemon::app::hasForeground())
            takeForegroundFromRunningApp(source);
        g_breezeHidden = false;
        g_pendingForegroundAppletHome = true;
        breezeOverlayTrace("%s HOME: SwitchU button used; closing Breeze for the SwitchU menu", source);
        return true;
    }

    if (!g_breezeSession || !g_foregroundAppletActive || g_breezeCloseForGame || g_breezeCloseForSleep)
        return false;

    const BreezeToggleMode toggle = readBreezeToggleMode();
    if (!g_breezeOverlayCapable && toggle == BreezeToggleMode::Overlay)
        g_breezeOverlayCapable = readBreezeOverlayCapability();

    // The toggle was changed away from Overlay in Breeze's settings: end the
    // overlay (Breeze gives the controller back) and handle HOME for the new mode.
    if (g_breezeOverlay != BreezeOverlayState::None && toggle != BreezeToggleMode::Overlay) {
        sendBreezeOverlay(smi::BreezeOverlayCommand::EnterNormal, 1000);
        g_breezeOverlay = BreezeOverlayState::None;
        if (toggle == BreezeToggleMode::Off) {
            // Off: HOME reaches the SwitchU menu. Close Breeze; the menu opens
            // once its applet has closed.
            if (daemon::app::isRunning() && daemon::app::hasForeground())
                takeForegroundFromRunningApp(source);
            g_breezeHidden = false;
            g_pendingForegroundAppletHome = true;
            breezeOverlayTrace("%s HOME: toggle is Off; closing Breeze for the SwitchU menu", source);
            return true;
        }
        breezeOverlayTrace("%s HOME: toggle left Overlay; overlay ended", source);
    }

    if (g_breezeOverlayCapable && daemon::app::isRunning() &&
        (g_breezeOverlay != BreezeOverlayState::None || toggle == BreezeToggleMode::Overlay) &&
        handleBreezeOverlayHome(source))
        return true;

    if (g_breezeHidden || (daemon::app::isRunning() && daemon::app::hasForeground())) {
        if (g_breezeStartedBackground && g_breezeHolder) {
            // Promote this background applet only for an explicit HOME press.
            // Do not replace it or request qlaunch foreground first.
            serviceAssumeDomain(&g_breezeHolder->s);
            const Result rc = serviceDispatch(&g_breezeHolder->s, 150);
            if (R_SUCCEEDED(rc)) {
                daemon::app::onHomeSuspend();
                g_breezeHidden = false;
            }
            switchu::FileLog::log("[%s] HOME -> background Breeze foreground rc=0x%X", source, rc);
            return true;
        }
        // Game in front, Breeze held behind it: reclaiming the foreground shows
        // Breeze again without restarting it.
        if (!takeForegroundFromRunningApp(source)) {
            switchu::FileLog::log("[%s] HOME -> Breeze aborted: foreground request failed",
                                  source);
            return true;
        }
        g_breezeHidden = false;
        switchu::FileLog::log("[%s] HOME -> Breeze (held)", source);
        return true;
    }

    if (!daemon::app::isRunning())
        return false; // nothing to toggle to: exit to the SwitchU menu as usual

    const BreezeToggleMode mode = effectiveBreezeMode();
    if (mode == BreezeToggleMode::Off)
        return false;

    if (mode == BreezeToggleMode::Keep || mode == BreezeToggleMode::Overlay) {
        const Result rc = daemon::app::resume();
        if (R_FAILED(rc)) {
            switchu::FileLog::log("[%s] HOME -> game resume FAIL: 0x%X", source, rc);
            return false;
        }
        g_breezeHidden = true;
        switchu::FileLog::log("[%s] HOME -> game, holding Breeze", source);
        return true;
    }

    g_breezeResumeOnClose = true;
    g_pendingForegroundAppletHome = true;
    switchu::FileLog::log("[%s] HOME -> game, closing Breeze for fast restart", source);
    return true;
}

// A remote NRO uses the same Album applet host as Breeze, but has independent
// HOME semantics: retain the NRO holder and toggle foreground with its game.
// Only handle HOME after a successful loader acknowledgement; failures and
// startup timeouts keep the normal applet-exit behavior.
static bool handleRemoteNroHome(const char* source) {
    if (!g_remoteNroActiveId || !g_foregroundAppletActive || !g_remoteNroAcked ||
        R_FAILED(g_remoteNroAckResult) || g_remoteNroTimedOut || g_breezeSession)
        return false;

    if (!daemon::app::isRunning())
        return false; // no game to toggle to; keep the normal HOME/exit path

    if (daemon::app::hasForeground()) {
        if (!takeForegroundFromRunningApp(source)) {
            switchu::FileLog::log("[%s] HOME -> remote NRO aborted: foreground request failed", source);
            return true;
        }
        g_remoteNroHidden = false;
        switchu::FileLog::log("[%s] HOME -> remote NRO, holding current NRO", source);
        return true;
    }

    const Result rc = daemon::app::resume();
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[%s] HOME -> game resume from remote NRO FAIL: 0x%X", source, rc);
        return true; // keep this NRO in front; a failed toggle must not close it
    }
    g_remoteNroHidden = true;
    switchu::FileLog::log("[%s] HOME -> game, holding current remote NRO", source);
    return true;
}

static void startPowerSequence(const char* source, smi::SystemMessage action);

// Sleeping with Breeze held behind the game left the game unresponsive after
// wake, and HOME and the power menu stopped reaching the daemon: a held
// AllForeground applet blocks the game's own applets, which games often open
// right after waking. Close the held Breeze first; the sleep starts once its
// applet has closed (see finishBreezeApplet).
static bool deferSleepForHeldBreeze(const char* source) {
    if (!g_breezeSession || !g_foregroundAppletActive || !g_breezeHidden)
        return false;
    if (!g_breezeCloseForSleep) {
        powerTrace("%s: sleep requested; closing held Breeze first (overlay_state=%d)",
                   source, static_cast<int>(g_breezeOverlay));
        releaseBreezeOverlayBeforeClose();
        g_breezeCloseForSleep = true;
        g_pendingForegroundAppletHome = true;
    }
    return true;
}

// Called after the Album / User Page applet closed. Returns true when the
// Breeze toggle handed the foreground to the game, so the menu stays closed.
// The system applet Breeze asked for in kBreezeOpenAppletFlag (removed here).
static bool takeBreezeAppletRequest(daemon::SystemActionType& type) {
    FILE* file = fopen(smi::kBreezeOpenAppletFlag, "rb");
    if (!file)
        return false;
    char word[32]{};
    const size_t n = fread(word, 1, sizeof(word), file);
    fclose(file);
    ::remove(smi::kBreezeOpenAppletFlag);
    switch (daemon::parseBreezeAppletRequest(std::string_view(word, n))) {
        case daemon::BreezeAppletRequest::Album: type = daemon::SystemActionType::OpenAlbum; return true;
        case daemon::BreezeAppletRequest::Mii: type = daemon::SystemActionType::OpenMiiEditor; return true;
        case daemon::BreezeAppletRequest::Remap: type = daemon::SystemActionType::OpenControllerRemapping; return true;
        case daemon::BreezeAppletRequest::NetConnect: type = daemon::SystemActionType::OpenNetConnect; return true;
        case daemon::BreezeAppletRequest::UserCreator: type = daemon::SystemActionType::OpenUserCreator; return true;
        default: switchu::FileLog::log("[breeze] invalid system applet request"); return false;
    }
}

static bool finishBreezeApplet(const char* name) {
    const bool wasBreeze = g_breezeSession;
    daemon::SystemActionType appletType{};
    const bool appletRequested = wasBreeze && takeBreezeAppletRequest(appletType);
    const bool resumeGame = g_breezeResumeOnClose;
    const bool closedForGame = g_breezeCloseForGame;
    const bool closedForSleep = g_breezeCloseForSleep;
    if (g_breezeSession)
        breezeOverlayTrace("%s applet closed overlay_state=%d", name, static_cast<int>(g_breezeOverlay));
    if (wasBreeze)
        destroyLeftoverOverlayLayers(name);
    if (g_breezeOverlay == BreezeOverlayState::Shown)
        restoreInputAfterBreezeOverlay();
    g_breezeOverlay = BreezeOverlayState::None;
    g_breezeOverlayCapable = false;
    g_breezeOverlayForegroundRequested = false;
    g_breezeCandidate = false;
    g_breezeSession = false;
    g_breezeStartedBackground = false;
    g_breezeHidden = false;
    g_breezeResumeOnClose = false;
    g_breezeCloseForGame = false;
    g_breezeCloseForSleep = false;
    ::remove(smi::kBreezeRunningFlag);
    ::remove(smi::kBreezeOpenMenuFlag);
    if (!wasBreeze)
        return false;

    // A PC replacement owns the next foreground transition. Do not reopen
    // HOME or resume the game between closing this holder and its replacement.
    if (g_remoteNroPending)
        return true;

    if (appletRequested && !closedForSleep) {
        // Breeze asked for a system applet and closed for it. A game stays
        // suspended behind it; Breeze opens again when the applet closes.
        daemon::SystemAction action{};
        action.type = appletType;
        action.uid = g_breezeUid;
        const Result rc = g_actionQueue.enqueue(action);
        if (R_SUCCEEDED(rc))
            g_breezeAppletReturn.arm();
        switchu::FileLog::log("[breeze] %s closed for applet type=%u rc=0x%X", name,
                              static_cast<u32>(appletType), rc);
        if (R_SUCCEEDED(rc))
            return true;
    }

    if (closedForSleep) {
        // HOME after wake reopens Breeze through the profile takeover. From
        // hbmenu that isn't possible, so HOME opens the SwitchU menu instead.
        g_breezeRelaunchPending = g_breezeDirect && daemon::app::isRunning();
        powerTrace("%s closed for sleep; relaunch_on_home=%d", name, g_breezeRelaunchPending ? 1 : 0);
        startPowerSequence("breeze-sleep", smi::SystemMessage::EnterSleep);
        return true;
    }

    if (closedForGame && daemon::app::isRunning()) {
        g_breezeRelaunchPending = g_breezeDirect;
        switchu::FileLog::log("[breeze] %s closed for a game applet; relaunch_on_home=%d",
                              name, g_breezeRelaunchPending ? 1 : 0);
        return true;
    }

    if (resumeGame && daemon::app::isRunning()) {
        const Result rc = daemon::app::resume();
        switchu::FileLog::log("[breeze] %s closed; resume game rc=0x%X", name, rc);
        if (R_SUCCEEDED(rc)) {
            g_breezeRelaunchPending = true;
            return true;
        }
    }

    // The runner skipped reclaiming the foreground for these cases.
    if (resumeGame || closedForGame)
        appletRequestToGetForeground();
    return false;
}

static Result startControlCacheWorker();
static void stopControlCacheWorker();
static Result startEventManager();
static void stopEventManager();

static bool menuInstalled() {
    return breezeFlagExists(smi::kMenuExecutablePath);
}

static bool breezeAlbumLoaderReady() {
    return breezeFlagExists(smi::kBreezeAlbumLoaderMain) &&
           breezeFlagExists(smi::kBreezeAlbumLoaderNpdm);
}

static bool breezeCanStart() {
    return breezeFlagExists(smi::kBreezeNroPath) &&
           (breezeFlagExists(smi::kBreezeUserPageLoaderPath) ||
            breezeAlbumLoaderReady());
}

static Result queueBreeze(AccountUid uid, const char* source, uint64_t returnSessionId = 0) {
    if (g_foregroundAppletActive && g_breezeCandidate) {
        switchu::FileLog::log("[%s] Breeze already open; not queued again", source);
        return 0;
    }
    daemon::SystemAction action{};
    action.type = daemon::SystemActionType::OpenBreeze;
    action.uid = uid;
    action.breezeReturnSessionId = returnSessionId;
    const Result rc = g_actionQueue.enqueue(action);
    switchu::FileLog::log("[%s] opening Breeze as HOME rc=0x%X", source, rc);
    return rc;
}

static void retryBreezeReturn(uint64_t sessionId, const char* reason) {
    // HOME/cancel must not re-arm a completed request, nor may an old attempt
    // replace the target of a more recent game launch.
    if (g_breezeRestoreTargetSessionId != sessionId)
        return;
    if (!daemon::canRetryBreezeReturn(sessionId, daemon::app::snapshot().sessionId,
                                      daemon::app::isRunning(), g_launchRequestPending)) {
        cancelBreezeReturn("retry target no longer current");
        return;
    }
    g_breezeLaunchHandoff.schedule(sessionId, armTicksToNs(armGetSystemTick()));
    switchu::FileLog::log("[breeze-return] retry in 2s: %s", reason);
}

static constexpr uint64_t kHomeFallbackIntervalNs = 3'000'000'000ULL;
static constexpr int kHomeFallbackCooldownTicks = 300;  // ~3 s of fast main-loop ticks

// HOME uses the SwitchU menu, or a real hbmenu when no menu is installed.
// The legacy breeze_first flag no longer redirects boot/exit/HOME to Breeze.
static Result openHome(smi::MenuStartMode mode, const char* source) {
    if (!menuInstalled()) {
        // Use an explicit path so a separate Album override cannot redirect
        // this fallback to Breeze. Rate-limit a missing/broken hbmenu.
        static uint64_t s_lastFallbackTick = 0;
        if (g_foregroundAppletActive || !g_actionQueue.empty())
            return 0;
        // Only the idle path (no game) can loop; with a game running this runs
        // on a HOME press and must always answer it.
        const uint64_t now = armGetSystemTick();
        if (!daemon::app::isRunning() && s_lastFallbackTick != 0 &&
            armTicksToNs(now - s_lastFallbackTick) < kHomeFallbackIntervalNs) {
            g_menuRelaunchCooldown = kHomeFallbackCooldownTicks;  // idle loop retries later
            return 0;
        }
        s_lastFallbackTick = now;
        daemon::SystemAction action{};
        action.type = daemon::SystemActionType::OpenHbmenu;
        const Result rc = g_actionQueue.enqueue(action);
        switchu::FileLog::log("[%s] no SwitchU menu; opening hbmenu rc=0x%X",
                              source, rc);
        return rc;
    }
    const Result rc = daemon::menu_la::launch(mode, buildSystemStatus());
    return rc;
}

static Result launchPendingHomeMenu() {
    if (!g_pendingHomeMenuLaunch)
        return 0;

    const char* source = g_pendingHomeMenuSource ? g_pendingHomeMenuSource : "home";
    const uint64_t startedAt = g_pendingHomeMenuStartedAt;
    const uint64_t breezeReturnSessionId = g_pendingBreezeReturnSessionId;
    g_pendingBreezeReturnSessionId = 0;
    g_pendingHomeMenuLaunch = false;
    g_pendingHomeMenuSource = nullptr;
    g_pendingHomeMenuStartedAt = 0;

    if (g_launchRequestPending) {
        switchu::FileLog::log("[%s] HOME foreground acquired; menu skipped for requested launch",
                              source);
        return 0;
    }

    if (breezeReturnSessionId != 0) {
        if (!daemon::app::isRunning() ||
            daemon::app::snapshot().sessionId != breezeReturnSessionId) {
            switchu::FileLog::log("[breeze-return] foreground acquired; stale session discarded");
            return 0;
        }
        const Result rc = queueBreeze(g_lastLaunchUid, source, breezeReturnSessionId);
        switchu::FileLog::log("[breeze-return] foreground confirmed; opening Breeze rc=0x%X", rc);
        if (R_FAILED(rc))
            daemon::app::resume();
        if (R_FAILED(rc))
            retryBreezeReturn(breezeReturnSessionId, "Breeze action queue busy");
        return rc;
    }

    if (g_breezeRelaunchPending) {
        g_breezeRelaunchPending = false;
        if (readBreezeToggleMode() != BreezeToggleMode::Off) {
            daemon::SystemAction action{};
            action.type = daemon::SystemActionType::OpenBreeze;
            action.uid = g_breezeUid;
            const Result queueRc = g_actionQueue.enqueue(action);
            switchu::FileLog::log(
                "[%s] HOME foreground acquired; relaunching Breeze rc=0x%X",
                source, queueRc);
            if (R_SUCCEEDED(queueRc)) {
                logHomeState(source, "after");
                return 0;
            }
        }
    }

    if (!menuInstalled()) {
        const Result homeRc = openHome(smi::MenuStartMode::MainMenu, source);
        logHomeState(source, "after");
        return homeRc;
    }

    const auto status = buildSystemStatus();
    switchu::FileLog::log(
        "[%s] HOME foreground acquired; launching MainMenu status.running=%d suspended=0x%016lX",
        source, status.app_running ? 1 : 0, status.suspended_app_id);
    const uint64_t launchStartedAt = armGetSystemTick();
    const Result rc = daemon::menu_la::launch(smi::MenuStartMode::MainMenu, status);
    const uint64_t launchDoneAt = armGetSystemTick();
    switchu::FileLog::log(
        "[%s] HOME MainMenu launch rc=0x%X foreground_wait=%lums launch=%lums total=%lums",
        source, rc,
        static_cast<unsigned long>(armTicksToNs(launchStartedAt - startedAt) / 1'000'000ULL),
        static_cast<unsigned long>(armTicksToNs(launchDoneAt - launchStartedAt) / 1'000'000ULL),
        static_cast<unsigned long>(armTicksToNs(launchDoneAt - startedAt) / 1'000'000ULL));
    if (R_SUCCEEDED(rc))
        armCatalogHold(kCatalogHoldAfterHomeNs);
    logHomeState(source, "after");
    return rc;
}

static void requestPowerStateChange(const char* source, bool reboot) {
    Result rc = spsmInitialize();
    if (R_SUCCEEDED(rc)) {
        rc = spsmShutdown(reboot);
        spsmExit();
        if (R_SUCCEEDED(rc))
            return;
    }

    char message[128]{};
    std::snprintf(message, sizeof(message),
                  "[power] %s spsm failed rc=0x%X; using applet fallback",
                  source, rc);
    svcOutputDebugString(message, std::strlen(message));
    if (reboot)
        appletStartRebootSequence();
    else
        appletStartShutdownSequence();
}

static void startPowerSequence(const char* source, smi::SystemMessage action) {
    if (g_pendingBreezeReturnSessionId != 0) {
        g_pendingHomeMenuLaunch = false;
        g_pendingHomeMenuSource = nullptr;
        g_pendingHomeMenuStartedAt = 0;
    }
    cancelBreezeReturn(source);
    cancelViewPolling(source);
    takeForegroundFromRunningApp(source);

    // Sleep keeps the filesystem mounted and the daemon alive. Stopping its
    // workers here would leave notifications and metadata caching disabled
    // after wake, which is an unsafe side effect of the fork implementation.
    if (action == smi::SystemMessage::EnterSleep) {
        appletStartSleepSequence(true);
        return;
    }

    g_powerSequenceStarted.store(true);

    // The menu committed its writers before sending this command. The daemon
    // can still write cache/catalog/log data in the small IPC gap, so join all
    // of its background writers and commit once more from this process.
    stopEventManager();
    stopControlCacheWorker();
    if (!switchu::commitSdCard("daemon power handoff")) {
        constexpr const char* failure =
            "[SwitchU-daemon] SD commit failed; power cancelled";
        svcOutputDebugString(failure, std::strlen(failure));
        g_powerSequenceStarted.store(false);
        startControlCacheWorker();
        startEventManager();
        return;
    }

    switch (action) {
        case smi::SystemMessage::Shutdown:
            requestPowerStateChange(source, false);
            break;
        case smi::SystemMessage::Reboot:
            requestPowerStateChange(source, true);
            break;
        default:
            break;
    }
}

static void openMenuFromHome(const char* source) {
    cancelBreezeReturn(source);
    const uint64_t homeStartedAt = armGetSystemTick();
    logHomeState(source, "request");
    cancelViewPolling("home");

    if (g_pendingHomeMenuLaunch) {
        switchu::FileLog::log("[%s] HOME ignored: foreground handoff already pending", source);
        return;
    }

    if (handleRemoteNroHome(source))
        return;

    if (handleBreezeHome(source))
        return;

    if (daemon::app::isRunning() && daemon::app::hasForeground()) {
        if (!takeForegroundFromRunningApp(source)) {
            switchu::FileLog::log("[%s] HOME aborted: foreground request failed", source);
            return;
        }

        // appletRequestToGetForeground() only queues the ownership transfer.
        // Creating an AllForeground library applet before AE confirms message 1
        // races qlaunch's own foreground transition and can crash the daemon.
        g_pendingHomeMenuLaunch = true;
        g_pendingHomeMenuSource = source;
        g_pendingHomeMenuStartedAt = homeStartedAt;
        switchu::FileLog::log("[%s] HOME waiting for ChangeIntoForeground", source);
        return;
    }

    if (daemon::menu_la::isActive()) {
        switchu::FileLog::log("[%s] HOME forwarding HomeRequest to active menu", source);
        pushNotification(smi::MenuMessage::HomeRequest);
    } else if (g_foregroundAppletActive) {
        switchu::FileLog::log("[%s] HOME requested while foreground applet active", source);
        g_pendingForegroundAppletHome = true;
    } else {
        switchu::FileLog::log("[%s] HOME no app/menu active; opening HOME", source);
        Result menuRc = openHome(smi::MenuStartMode::MainMenu, source);
        switchu::FileLog::log("[%s] HOME MainMenu launch rc=0x%X", source, menuRc);
        if (R_SUCCEEDED(menuRc))
            armCatalogHold(kCatalogHoldAfterMenuLaunchNs);
    }
}

static bool sendViewFlagsUpdates() {
    std::vector<switchu::ns::ExtApplicationRecord> records;
    if (!listApplicationRecords(records, "views"))
        return false;

    std::vector<switchu::ns::ExtApplicationView> views;
    queryApplicationViews(records, views, "views");

    const s32 count = static_cast<s32>(records.size());

    if (count != g_lastRecordCount) {
        const s32 prevCount = g_lastRecordCount;
        switchu::FileLog::log("[views] title count changed %d -> %d, full reload needed",
                              g_lastRecordCount, count);

        for (s32 i = 0; i < count; ++i) {
            g_lastRecordTids[i] = records[i].id;
            g_lastViewFlags[i]  = views[i].flags;
        }
        for (s32 i = count; i < prevCount && i < kMaxTrackedApplicationRecords; ++i) {
            g_lastRecordTids[i] = 0;
            g_lastViewFlags[i]  = 0;
        }
        g_lastRecordCount = count;

        return true;
    }

    int pushed = 0;
    for (s32 i = 0; i < count; ++i) {
        uint32_t newFlags = views[i].flags;
        uint32_t oldFlags = 0;
        for (s32 j = 0; j < g_lastRecordCount; ++j) {
            if (g_lastRecordTids[j] == records[i].id) {
                oldFlags = g_lastViewFlags[j];
                break;
            }
        }
        if (newFlags != oldFlags) {
            pushNotification(smi::MenuMessage::AppViewFlagsUpdate,
                             records[i].id, newFlags);
            ++pushed;
        }
        g_lastRecordTids[i] = records[i].id;
        g_lastViewFlags[i]  = newFlags;
    }
    g_lastRecordCount = count;

    switchu::FileLog::log("[views] checked %d titles, pushed %d flag updates",
                          count, pushed);
    return false;
}

static void handleGeneralChannel() {
    AppletStorage st;
    if (R_FAILED(appletPopFromGeneralChannel(&st))) return;

    struct SamsHeader {
        u32 magic;
        u32 version;
        u32 msg;
        u32 reserved;
    } hdr = {};

    s64 sz = 0;
    appletStorageGetSize(&st, &sz);
    if (sz > 0)
        appletStorageRead(&st, 0, &hdr, (size_t)sz < sizeof(hdr) ? (size_t)sz : sizeof(hdr));
    appletStorageClose(&st);

    if (hdr.magic != 0x534D4153) return;

    switchu::FileLog::log("[sams] msg=%u", hdr.msg);
    switch (hdr.msg) {
        case 2:
        switchu::FileLog::log("[sams] -> Home");
        openMenuFromHome("sams");
        break;
        case 3:
        powerTrace("sams -> Sleep");
        if (deferSleepForHeldBreeze("sams"))
            break;
        startPowerSequence("sams-sleep", smi::SystemMessage::EnterSleep);
        break;
        case 5:
        switchu::FileLog::log("[sams] -> Shutdown");
        startPowerSequence("sams-shutdown", smi::SystemMessage::Shutdown);
        break;
        case 6:
        switchu::FileLog::log("[sams] -> Reboot");
        startPowerSequence("sams-reboot", smi::SystemMessage::Reboot);
        break;
    }
}

static void onLaunchRequested() {
    cancelBreezeReturn("new launch request");
    g_launchRequestFromBreeze = daemon::isBreezeStartRequest(
        g_foregroundAppletActive, g_breezeCandidate,
        g_breezeSession || breezeFlagExists(smi::kBreezeRunningFlag),
        daemon::app::isRunning());
    switchu::FileLog::log(
        "[ae] -> LaunchApplicationRequested appRunning=%d hasFG=%d menu=%d fgApplet=%d breeze=%d held=%d",
        daemon::app::isRunning() ? 1 : 0, daemon::app::hasForeground() ? 1 : 0,
        daemon::menu_la::hasHolder() ? 1 : 0, g_foregroundAppletActive ? 1 : 0,
        g_breezeSession ? 1 : 0, g_breezeHidden ? 1 : 0);
    g_launchRequestPending = true;
    // Only an application that is in front can be the one asking; with an applet
    // or the menu in front, it is that one.
    g_launchRequestFromApplet = !(daemon::app::isRunning() && daemon::app::hasForeground());
    switchu::FileLog::log("[ae] launch requested by %s",
                          g_launchRequestFromApplet ? "an applet" : "the application");

    if (g_pendingHomeMenuLaunch) {
        switchu::FileLog::log("[ae] clearing pending HOME foreground handoff: launch requested");
        g_pendingHomeMenuLaunch = false;
        g_pendingHomeMenuSource = nullptr;
        g_pendingHomeMenuStartedAt = 0;
    }

    // The requester is usually the applet in front (hbmenu / sphaira / Breeze
    // in Album or User Page). Close it; mainLoop launches once nothing holds
    // the foreground, and relaunchMenuAfterApplet stays out of the way.
    if (g_foregroundAppletActive) {
        releaseBreezeOverlayBeforeClose();
        g_pendingForegroundAppletHome = true;
    } else if (daemon::menu_la::hasHolder()) {
        const Result rc = daemon::menu_la::terminate();
        switchu::FileLog::log("[ae] menu closed for requested launch rc=0x%X", rc);
    }
}

// Preferred user, else the last menu launch's, else the system's pick, else the first.
static AccountUid resolveUser(AccountUid preferred) {
    AccountUid uid = accountUidIsValid(&preferred) ? preferred : g_lastLaunchUid;
    if (!accountUidIsValid(&uid)) {
        Result rc = accountTrySelectUserWithoutInteraction(&uid, false);
        if (R_FAILED(rc) || !accountUidIsValid(&uid)) {
            AccountUid users[ACC_USER_LIST_SIZE]{};
            s32 userCount = 0;
            rc = accountListAllUsers(users, ACC_USER_LIST_SIZE, &userCount);
            uid = (R_SUCCEEDED(rc) && userCount > 0) ? users[0] : AccountUid{};
        }
    }
    return uid;
}

// The menu's "default profile" setting. The menu writes the uid only while the
// setting is on, so a well-formed uid of a user that still exists means it is on.
static bool readMenuDefaultProfile(AccountUid& out) {
    std::ifstream f("sdmc:/config/SwitchU/settings.json");
    if (!f.is_open())
        return false;
    const std::string text((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
    const std::size_t key = text.find("\"defaultProfileUid\"");
    const std::size_t open = key == std::string::npos ? key
        : text.find('"', text.find(':', key));
    if (open == std::string::npos || open + 33 >= text.size() || text[open + 33] != '"')
        return false;

    AccountUid uid{};
    for (int part = 0; part < 2; ++part) {
        for (int i = 0; i < 16; ++i) {
            const char ch = text[open + 1 + part * 16 + i];
            const int nibble = ch >= '0' && ch <= '9' ? ch - '0'
                : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
                : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
            if (nibble < 0)
                return false;
            uid.uid[part] = (uid.uid[part] << 4) | static_cast<u64>(nibble);
        }
    }

    AccountUid users[ACC_USER_LIST_SIZE]{};
    s32 userCount = 0;
    if (!accountUidIsValid(&uid) ||
        R_FAILED(accountListAllUsers(users, ACC_USER_LIST_SIZE, &userCount)))
        return false;
    for (s32 i = 0; i < userCount; ++i) {
        if (users[i].uid[0] == uid.uid[0] && users[i].uid[1] == uid.uid[1]) {
            out = uid;
            return true;
        }
    }
    return false;
}

// Same decision the menu makes before its own launches (default profile, then a
// silent pick, then asking), except that the question is the system's
// playerSelect applet, as on Nintendo's HOME. Titles that do not need a user
// keep the old silent choice.
static Result chooseRequestedLaunchUser(uint64_t titleId, AccountUid* outUid) {
    uint8_t account = 1;
    uint8_t option = 0;
    switchu::control_cache::Meta meta{};
    if (switchu::control_cache::readOrFetchMeta(titleId, meta)) {
        account = meta.startup_user_account;
        option = meta.startup_user_account_option;
    }

    *outUid = AccountUid{};
    if (account == 0)
        return 0;
    if (g_launchRequestFromBreeze) {
        // PC Connect cannot answer a playerSelect applet after Breeze closes.
        // Use the first system-listed user, not the last/default user. Failure
        // stays a failure instead of silently opening a console-only dialog.
        AccountUid users[ACC_USER_LIST_SIZE]{};
        s32 count = 0;
        const Result rc = accountListAllUsers(users, ACC_USER_LIST_SIZE, &count);
        if (R_FAILED(rc))
            return rc;
        if (count <= 0 || !accountUidIsValid(&users[0]))
            return MAKERESULT(Module_Libnx, LibnxError_NotFound);
        *outUid = users[0];
        g_lastLaunchUid = *outUid;
        switchu::FileLog::log("[launch-request] Breeze PC start: first user selected");
        return 0;
    }
    if (account != 2 && option != 0) {
        *outUid = resolveUser(AccountUid{});
        return 0;
    }

    const bool networkRequired = account == 2;
    AccountUid uid{};
    if (!networkRequired && readMenuDefaultProfile(uid)) {
        *outUid = uid;
        return 0;
    }
    uid = AccountUid{};
    Result rc = accountTrySelectUserWithoutInteraction(&uid, networkRequired);
    if (R_SUCCEEDED(rc) && accountUidIsValid(&uid)) {
        *outUid = uid;
        return 0;
    }

    switchu::FileLog::log("[launch-request] asking for a user title=0x%016lX nsa=%d",
                          titleId, networkRequired ? 1 : 0);
    appletRequestToGetForeground();
    g_foregroundAppletActive = true;
    g_pendingForegroundAppletHome = false;
    // pselShowUserSelectorForLauncher() would first call
    // accountIsUserRegistrationRequestPermitted(), which acc denies us (see the
    // user creator), so fill in the launcher settings directly.
    PselUiSettings ui{};
    rc = pselUiCreate(&ui, PselUiMode_UserSelector);
    if (R_SUCCEEDED(rc)) {
        ui.settings.application_id = titleId;
        ui.settings.is_network_service_account_required = networkRequired ? 1 : 0;
        ui.settings.unk_x92 = 1;
        ui.settings.unk_x96 = 1;
        uid = AccountUid{};
        rc = pselUiShow(&ui, &uid);
    }
    g_foregroundAppletActive = false;
    g_pendingForegroundAppletHome = false;
    switchu::FileLog::log("[launch-request] user selector rc=0x%X selected=%d",
                          rc, accountUidIsValid(&uid) ? 1 : 0);
    if (R_FAILED(rc))
        return rc;
    if (!accountUidIsValid(&uid))
        return MAKERESULT(124, 1); // nn::account::ResultCancelledByUser
    *outUid = uid;
    return 0;
}

static void scheduleBreezeReturnAfterGameStart() {
    g_breezeReturnWaitLogged = false;
    const auto sessionId = daemon::app::snapshot().sessionId;
    g_breezeRestoreTargetSessionId = sessionId;
    g_breezeLaunchHandoff.schedule(sessionId, armTicksToNs(armGetSystemTick()));
    switchu::FileLog::log("[launch-request] Breeze return scheduled in 2s");
}

static Result launchRequestedApplication() {
    const Result rc = daemon::app::launchRequested(chooseRequestedLaunchUser,
                                                  g_launchRequestFromApplet);
    if (R_SUCCEEDED(rc)) {
        noteApplicationLaunched();
        // Keep the PC game-start handoff. This return is tied to this game
        // session; an explicit different-NRO launch still cancels it.
        if (g_launchRequestFromBreeze)
            scheduleBreezeReturnAfterGameStart();
    }
    g_launchRequestFromBreeze = false;
    switchu::FileLog::log("[launch-request] rc=0x%X title=0x%016lX", rc,
                          daemon::app::suspendedTitleId());
    return rc;
}

static bool consumeLaunchRequest() {
    if (!g_launchRequestPending || daemon::menu_la::hasHolder() || g_foregroundAppletActive)
        return false;
    g_launchRequestPending = false;

    if (R_FAILED(launchRequestedApplication()) && !daemon::app::isRunning())
        openHome(smi::MenuStartMode::MainMenu, "launch-request");
    return true;
}

static bool restoreBreezeAfterLaunch() {
    if (!g_breezeLaunchHandoff.pending())
        return false;
    // An explicit HOME/menu/action, a replacement launch or an exited game
    // takes priority. Never reopen Breeze against a later application session.
    if (!daemon::app::isRunning() || g_launchRequestPending || g_pendingHomeMenuLaunch ||
        daemon::menu_la::hasHolder() || g_foregroundAppletActive ||
        !g_actionQueue.empty()) {
        switchu::FileLog::log("[breeze-return] unavailable: game=%d request=%d home=%d menu=%d applet=%d action=%d nro=%d album=%d profile=%d",
            daemon::app::isRunning(), g_launchRequestPending, g_pendingHomeMenuLaunch,
            daemon::menu_la::hasHolder(), g_foregroundAppletActive, !g_actionQueue.empty(),
            breezeFlagExists(smi::kBreezeNroPath), breezeAlbumLoaderReady(),
            breezeFlagExists(smi::kBreezeUserPageLoaderPath));
        cancelBreezeReturn("foreground busy or loader missing");
        return false;
    }
    const uint64_t expectedSessionId = g_breezeLaunchHandoff.sessionId();
    const uint64_t sessionId = daemon::app::snapshot().sessionId;
    if (!g_breezeLaunchHandoff.due(sessionId, armTicksToNs(armGetSystemTick()))) {
        if (!g_breezeLaunchHandoff.pending()) {
            switchu::FileLog::log("[breeze-return] cancelled: %s",
                sessionId != expectedSessionId ? "application session changed" : "startup applet wait timed out");
            retryBreezeReturn(expectedSessionId, "startup applet wait expired");
        }
        return false;
    }

    if (!breezeCanStart()) {
        if (!g_breezeReturnWaitLogged) {
            switchu::FileLog::log("[breeze-return] waiting for Breeze.nro/loader files: nro=%d album=%d profile=%d",
                breezeFlagExists(smi::kBreezeNroPath), breezeAlbumLoaderReady(),
                breezeFlagExists(smi::kBreezeUserPageLoaderPath));
            g_breezeReturnWaitLogged = true;
        }
        g_breezeLaunchHandoff.schedule(sessionId, armTicksToNs(armGetSystemTick()));
        return false;
    }

    // Don't interrupt an applet the game opens at startup. Wait, bounded by
    // the handoff timeout, while still servicing HOME, sleep and application exit.
    bool appletsLeft = false;
    const Result appletRc = daemon::app::areLibraryAppletsLeft(&appletsLeft);
    if (R_FAILED(appletRc) || appletsLeft) {
        if (!g_breezeReturnWaitLogged) {
            switchu::FileLog::log("[breeze-return] waiting for startup applet: rc=0x%X left=%d", appletRc, appletsLeft);
            g_breezeReturnWaitLogged = true;
        }
        return false;
    }
    g_breezeLaunchHandoff.cancel(); // one attempt, including foreground/queue failure
    if (g_breezeBackgroundFailedSessionId != sessionId) {
        const Result rc = queueBreeze(g_lastLaunchUid, "breeze-pc-background", sessionId);
        switchu::FileLog::log("[breeze-return] queued background start; game keeps foreground rc=0x%X", rc);
        if (R_FAILED(rc))
            retryBreezeReturn(sessionId, "background queue busy");
        return R_SUCCEEDED(rc);
    }
    if (!takeForegroundFromRunningApp("breeze-pc-start")) {
        retryBreezeReturn(sessionId, "foreground request failed");
        return false;
    }
    // Just like a physical HOME request, wait for AM message 1. Requesting
    // foreground only queues the transfer; opening an AllForeground applet
    // on the next tick races that transfer.
    g_pendingBreezeReturnSessionId = sessionId;
    g_pendingHomeMenuLaunch = true;
    g_pendingHomeMenuSource = "breeze-pc-start";
    g_pendingHomeMenuStartedAt = armGetSystemTick();
    switchu::FileLog::log("[breeze-return] waiting for ChangeIntoForeground");
    return true;
}

static void handleAppletMessages() {
    u32 msg = 0;
    Result rc = appletGetMessage(&msg);
    if (R_FAILED(rc))
        return;

    switchu::FileLog::log("[ae] msg=%u", msg);
    switch (msg) {
        case 1:
        switchu::FileLog::log("[ae] -> ChangeIntoForeground");
        g_daemonForegroundKnown = true;
        if (!g_remoteNroPending)
            launchPendingHomeMenu();
        break;

        case 2:
        // AppletMessage_ChangeIntoBackground: another foreground participant is
        // taking over. The menu no longer stays alive in a hidden suspended
        // state; only a Breeze applet held behind the game may need to make way.
        switchu::FileLog::log("[ae] -> ChangeIntoBackground");
        g_daemonForegroundKnown = false;
        closeHeldBreezeIfGameNeedsApplet("ChangeIntoBackground");
        break;

        case 20:
        openMenuFromHome("ae");
        break;

        case 50:
        onLaunchRequested();
        break;

        case 22:
        case 29:
        case 32:
        powerTrace("ae -> Sleep (msg=%u)", msg);
        if (deferSleepForHeldBreeze("ae"))
            break;
        // The application keeps its IApplicationAccessor across system sleep,
        // but must reacquire the foreground after wake. Keep our session state
        // in sync so the Wakeup path is allowed to call app::resume().
        daemon::app::onHomeSuspend();
        powerTrace("ae sleep: starting sleep sequence");
        appletStartSleepSequence(true);
        powerTrace("ae sleep: sleep sequence returned");
        break;

        case 26:
        powerTrace("ae -> Wakeup");
        g_batteryRefreshPending.store(true);
        // Breeze in front of a suspended game stays in front after wake.
        if (daemon::app::isRunning() && !daemon::menu_la::isActive() &&
            !(g_breezeSession && !g_breezeHidden)) {
            Result rc = daemon::app::resume();
            if (R_FAILED(rc)) {
                switchu::FileLog::log("[ae] wake resume FAIL: 0x%X", rc);
                appletRequestToGetForeground();
            } else {
                switchu::FileLog::log("[ae] wake resume OK");
            }
        } else {
            appletRequestToGetForeground();
        }
        if (daemon::menu_la::isActive()) {
            pushNotification(smi::MenuMessage::WakeUp);
        } else if (!daemon::app::isRunning()) {
            openHome(smi::MenuStartMode::MainMenu, "wake");
        }
        break;
    }
}

static void pollRemoteNroRequests() {
    if (!g_remoteNroPending) {
        daemon::remote::Request request{};
        if (!daemon::remote::take(request))
            return;
        if (g_powerSequenceStarted.load() || g_launchRequestPending ||
            g_pendingHomeMenuLaunch || !g_actionQueue.empty()) {
            daemon::remote::finish(request.id, "failed", daemon::SystemActionQueue::kBusyResult);
            return;
        }
        if (request.kind == daemon::remote::RequestKind::StartGame) {
            // Never replace a running game implicitly or close its NRO on rejection.
            daemon::app::checkFinished();
            if (daemon::app::isRunning()) {
                daemon::remote::finish(request.id, "failed", daemon::SystemActionQueue::kBusyResult);
                return;
            }
            std::vector<switchu::ns::ExtApplicationRecord> records;
            if (!listApplicationRecords(records, "remote-game") ||
                std::none_of(records.begin(), records.end(), [&](const auto& record) {
                    return record.id == request.titleId;
                })) {
                daemon::remote::finish(request.id, "failed", MAKERESULT(Module_Libnx, LibnxError_NotFound));
                return;
            }
        } else {
            // The transport validates syntax; reject a missing/non-NRO target
            // before closing the user's current applet.
            unsigned char header[0x14]{};
            FILE* file = std::fopen(request.path.c_str(), "rb");
            const bool valid = file && std::fread(header, 1, sizeof(header), file) == sizeof(header)
                && std::memcmp(header + 0x10, "NRO0", 4) == 0;
            if (file)
                std::fclose(file);
            if (!valid) {
                daemon::remote::finish(request.id, "failed", MAKERESULT(Module_Libnx, LibnxError_BadInput));
                switchu::FileLog::log("[remote-nro] rejected missing/invalid target id=%lu path=%s", request.id, request.path.c_str());
                return;
            }
        }
        cancelBreezeReturn("remote-nro");
        g_breezeRelaunchPending = false;
        g_remoteNroRequest = std::move(request);
        g_remoteNroPending = true;
        g_remoteNroForegroundRequested = false;
        g_remoteNroReceivedAt = armGetSystemTick();
        if (g_foregroundAppletActive || daemon::menu_la::hasHolder() ||
            (daemon::app::isRunning() && daemon::app::hasForeground()))
            g_daemonForegroundKnown = false;
        switchu::FileLog::log("[remote-nro] accepted id=%lu path=%s", g_remoteNroRequest.id, g_remoteNroRequest.path.c_str());
    }
    if (g_foregroundAppletActive)
        g_pendingForegroundAppletHome = true;
}

static void pumpForegroundAppletMessages() {
    handleGeneralChannel();
    handleAppletMessages();
    pollRemoteNroRequests();
}

static void drainBreezeLaunchMessages() {
    // Breeze may exit between two runner polls. AM has already queued msg 50
    // when the holder joins; consume it before clearing the caller's identity.
    Event* event = appletGetMessageEvent();
    for (unsigned count = 0; event && count < 32; ++count) {
        if (R_FAILED(eventWait(event, 0)))
            break;
        handleAppletMessages();
    }
}

static constexpr uint64_t kBreezePollIntervalNs = 250'000'000ULL;

static void pumpBreezeAppletMessages() {
    pumpForegroundAppletMessages();
    if (!g_breezeCandidate)
        return;

    const uint64_t now = armGetSystemTick();
    if (g_breezePollTick != 0 && armTicksToNs(now - g_breezePollTick) < kBreezePollIntervalNs)
        return;
    g_breezePollTick = now;

    if (!g_breezeSession) {
        if (!breezeFlagExists(smi::kBreezeRunningFlag)) {
            // Probe the owned loader every two seconds. A cold Breeze launch
            // can take several seconds; never create overlapping applets.
            if (g_breezeReturnAttemptSessionId != 0 &&
                g_breezeRestoreTargetSessionId == g_breezeReturnAttemptSessionId &&
                (g_breezeReturnLastProbeAt == 0 ||
                 elapsedNsSince(g_breezeReturnLastProbeAt) >= 2'000'000'000ULL)) {
                g_breezeReturnLastProbeAt = now;
                if (elapsedNsSince(g_breezeReturnAttemptStartedAt) >= 10'000'000'000ULL) {
                    switchu::FileLog::log("[breeze-return] loader did not announce Breeze within 10s; closing before retry");
                    g_breezeReturnAttemptTimedOut = true;
                    g_pendingForegroundAppletHome = true;
                }
            }
            return;
        }
        g_breezeSession = true;
        g_breezeOverlayCapable = readBreezeOverlayCapability();
        if (g_breezeReturnAttemptSessionId != 0 &&
            g_breezeRestoreTargetSessionId == g_breezeReturnAttemptSessionId) {
            if (g_breezeStartedBackground) {
                g_breezeHidden = true;
                switchu::FileLog::log("[breeze-return] background Breeze ready; foreground untouched");
            } else if (daemon::app::isRunning()) {
                // Compatibility fallback: enter hidden overlay where supported,
                // then hand input/foreground back as soon as Breeze is ready.
                bool overlayHidden = false;
                if (readBreezeToggleMode() == BreezeToggleMode::Overlay && g_breezeOverlayCapable)
                    overlayHidden = sendBreezeOverlay(smi::BreezeOverlayCommand::EnterHidden, 1500);
                const Result resumeRc = daemon::app::resume();
                if (R_SUCCEEDED(resumeRc)) {
                    g_breezeHidden = true;
                    if (overlayHidden)
                        g_breezeOverlay = BreezeOverlayState::Hidden;
                }
                switchu::FileLog::log("[breeze-return] foreground fallback ready; game resume rc=0x%X", resumeRc);
            }
            g_breezeRestoreTargetSessionId = 0;
            switchu::FileLog::log("[breeze-return] Breeze announced running; automatic retries finished");
        }
        g_breezeOverlayCapable = readBreezeOverlayCapability();
        breezeOverlayTrace("session started via %s overlay_capable=%d toggle=%d",
                           g_breezeSlotName,
                           g_breezeOverlayCapable ? 1 : 0, static_cast<int>(readBreezeToggleMode()));
    }

    // Breeze closed itself for a system applet (kBreezeOpenAppletFlag), and
    // hbloader went on to hbmenu in the same applet. Close it as HOME would,
    // without waiting for HOME; finishBreezeApplet then starts the applet.
    if (!breezeFlagExists(smi::kBreezeRunningFlag) && breezeFlagExists(smi::kBreezeOpenAppletFlag) &&
        !g_pendingForegroundAppletHome) {
        g_pendingForegroundAppletHome = true;
        breezeOverlayTrace("Breeze exited for an applet; closing hbmenu");
    }
    // Retain the caller identity until finishBreezeApplet consumes interface5.
    // Otherwise the normal missing-running-flag cleanup can classify hbmenu
    // first and lose this request while the holder is closing.
    if (g_pendingForegroundAppletHome && breezeFlagExists(smi::kBreezeOpenAppletFlag))
        return;

    if (g_breezeOverlay != BreezeOverlayState::None) {
        drainBreezeOverlayMessages(smi::BreezeOverlayCommand::Ack);
        if (g_breezeOverlayForegroundRequested) {
            // Breeze is exiting from the overlay; its applet (hbmenu) needs the
            // foreground to be usable again.
            g_breezeOverlayForegroundRequested = false;
            g_breezeOverlay = BreezeOverlayState::None;
            if (takeForegroundFromRunningApp("breeze-overlay"))
                g_breezeHidden = false;
            breezeOverlayTrace("overlay closed by Breeze; applet back in front");
        }
    }

    // The loader can remain alive after Breeze exits into hbmenu/another NRO.
    // Do not keep treating that different NRO as a Breeze HOME session.
    if (breezeFlagExists(smi::kBreezeRunningFlag)) {
        g_breezeMissingSince = 0;
    } else if (g_breezeOverlay == BreezeOverlayState::None) {
        // Exit may remove the flag just before AM delivers a game-start
        // request. Drain it and retain the caller identity for one poll so
        // first-user selection and the PC start handoff stay attributed.
        drainBreezeLaunchMessages();
        if (g_breezeMissingSince == 0)
            g_breezeMissingSince = now;
        if (!g_launchRequestPending &&
            elapsedNsSince(g_breezeMissingSince) >= kBreezePollIntervalNs) {
            g_breezeSession = false;
            g_breezeDirect = false;
            g_breezeOverlayCapable = false;
            g_breezeRelaunchPending = false;
        }
    }

    // mainLoop is blocked while the applet runs, so watch the game here.
    if (daemon::app::checkFinished()) {
        switchu::FileLog::log("[breeze] game exited while Breeze open (held=%d)",
                              (g_breezeHidden || g_remoteNroHidden) ? 1 : 0);
        cancelBreezeReturn("game exited while loader open");
        if (g_breezeOverlay != BreezeOverlayState::None) {
            sendBreezeOverlay(smi::BreezeOverlayCommand::EnterNormal, 1000);
            g_breezeOverlay = BreezeOverlayState::None;
        }
        g_pendingHomeMenuLaunch = false;
        g_pendingHomeMenuSource = nullptr;
        g_pendingHomeMenuStartedAt = 0;
        if (g_breezeHidden) {
            g_breezeHidden = false;
            appletRequestToGetForeground();
        }
        if (g_remoteNroHidden) {
            g_remoteNroHidden = false;
            appletRequestToGetForeground();
        }
        return;
    }

    closeHeldBreezeIfGameNeedsApplet("poll");
}

static bool consumeForegroundAppletHomeRequest() {
    if (!g_pendingForegroundAppletHome)
        return false;
    g_pendingForegroundAppletHome = false;
    return true;
}

static Result launchLibraryApplet(AppletId id, const char* name,
                                  const void* inData = nullptr, size_t inDataSize = 0,
                                  u32 libAppletVersion = 0, bool breezeCandidate = false,
                                  LibAppletMode mode = LibAppletMode_AllForeground) {
    switchu::FileLog::log("[applet] launching %s id=0x%X version=0x%X in=%zu",
                          name, (u32)id, libAppletVersion, inDataSize);
    Result fgRc = mode == LibAppletMode_Background ? 0 : appletRequestToGetForeground();
    switchu::FileLog::log("[applet] %s RequestToGetForeground rc=0x%X", name, fgRc);

    g_foregroundAppletActive = true;
    g_pendingForegroundAppletHome = false;
    daemon::LibraryAppletInput input{inData, inDataSize};
    const daemon::LibraryAppletRequest request{
        .id = id,
        .name = name,
        .version = libAppletVersion,
        .pushCommonArgs = libAppletVersion != 0,
        .playStartupSound = mode != LibAppletMode_Background,
        .inputs = inData && inDataSize ? &input : nullptr,
        .inputCount = inData && inDataSize ? 1U : 0U,
        .skipForegroundRestore = breezeCandidate ? breezeSkipForegroundRestore : nullptr,
        .terminateOnExit = breezeCandidate ? breezeTerminateOnExit : nullptr,
        .activeHolder = breezeCandidate ? &g_breezeHolder : nullptr,
        .mode = mode,
    };
    const Result rc = daemon::runLibraryApplet(
        request,
        breezeCandidate ? pumpBreezeAppletMessages : pumpForegroundAppletMessages,
        consumeForegroundAppletHomeRequest);
    if (breezeCandidate)
        drainBreezeLaunchMessages();
    g_foregroundAppletActive = false;
    return rc;
}

static u32 controllerAppletVersion() {
    if (hosversionAtLeast(11, 0, 0)) return 0x8;
    if (hosversionAtLeast(8, 0, 0)) return 0x7;
    if (hosversionAtLeast(6, 0, 0)) return 0x5;
    if (hosversionAtLeast(3, 0, 0)) return 0x4;
    return 0x3;
}

static Result setupControllerPrivateArg(HidLaControllerSupportArgPrivate& privateArg,
                                        HidLaControllerSupportMode mode,
                                        size_t publicArgSize,
                                        bool homeMenuStyle) {
    privateArg.private_size = sizeof(privateArg);
    privateArg.arg_size = publicArgSize;
    privateArg.flag0 = homeMenuStyle ? 1 : 0;
    privateArg.flag1 = 1;
    privateArg.mode = mode;
    if (hosversionAtLeast(3, 0, 0)) {
        Result setupRc = hidGetSupportedNpadStyleSet(&privateArg.npad_style_set);
        HidNpadJoyHoldType holdType{};
        if (R_SUCCEEDED(setupRc))
            setupRc = hidGetNpadJoyHoldType(&holdType);
        privateArg.npad_joy_hold_type = holdType;
        return setupRc;
    } else {
        privateArg.npad_style_set = 0;
        privateArg.npad_joy_hold_type = HidNpadJoyHoldType_Horizontal;
    }
    return 0;
}

static Result runControllerApplet(const char* name,
                                  HidLaControllerSupportArgPrivate& privateArg,
                                  const void* publicArg,
                                  size_t publicArgSize) {
    HidLaControllerSupportResultInfoInternal output{};
    const daemon::LibraryAppletInput inputs[] = {
        {&privateArg, sizeof(privateArg)},
        {publicArg, publicArgSize},
    };
    daemon::LibraryAppletRequest request{
        .id = AppletId_LibraryAppletController,
        .name = name,
        .version = controllerAppletVersion(),
        .playStartupSound = true,
        .inputs = inputs,
        .inputCount = 2,
        .output = &output,
        .outputSize = sizeof(output),
    };

    appletRequestToGetForeground();
    g_foregroundAppletActive = true;
    g_pendingForegroundAppletHome = false;
    Result rc = daemon::runLibraryApplet(
        request, pumpForegroundAppletMessages,
        consumeForegroundAppletHomeRequest);
    g_foregroundAppletActive = false;
    switchu::FileLog::log(
        "[applet] %s output res=0x%X players=%d selected=%u runner=0x%X",
        name, output.res, output.info.player_count, output.info.selected_id, rc);
    if (R_SUCCEEDED(rc) && output.res == 1) {
        switchu::FileLog::log("[applet] %s completed outcome=cancelled", name);
    } else if (R_SUCCEEDED(rc) && output.res != 0) {
         rc = MAKERESULT(Module_Libnx, LibnxError_LibAppletBadExit);
    }
    return rc;
}

static Result launchControllerPairing() {
    switchu::FileLog::log("[applet] launching Controller pairing");
    HidLaControllerSupportArg arg;
    hidLaCreateControllerSupportArg(&arg);
    arg.hdr.player_count_max = 8;
    arg.hdr.enable_single_mode = false;

    HidLaControllerSupportArgV3 legacyArg{};
    const void* publicArg = &arg;
    size_t publicArgSize = sizeof(arg);
    if (hosversionBefore(8, 0, 0)) {
        legacyArg.hdr = arg.hdr;
        std::memcpy(legacyArg.identification_color, arg.identification_color,
                    sizeof(legacyArg.identification_color));
        legacyArg.enable_explain_text = arg.enable_explain_text;
        std::memcpy(legacyArg.explain_text, arg.explain_text,
                    sizeof(legacyArg.explain_text));
        legacyArg.hdr.player_count_min = std::min<s8>(legacyArg.hdr.player_count_min, 4);
        legacyArg.hdr.player_count_max = std::min<s8>(legacyArg.hdr.player_count_max, 4);
        publicArg = &legacyArg;
        publicArgSize = sizeof(legacyArg);
    }

    HidLaControllerSupportArgPrivate privateArg{};
    Result rc = setupControllerPrivateArg(
        privateArg, HidLaControllerSupportMode_ShowControllerSupport,
        publicArgSize, true);
    if (R_SUCCEEDED(rc))
        rc = runControllerApplet("Controllers", privateArg, publicArg, publicArgSize);
    if (R_FAILED(rc))
        switchu::FileLog::log("[applet] Controller FAIL: 0x%X", rc);
    else
        switchu::FileLog::log("[applet] Controller pairing done");
    return rc;
}

static Result launchControllerRemapping() {
    if (hosversionBefore(11, 0, 0))
        return MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer);

    HidLaControllerKeyRemappingArg arg{};
    hidLaCreateControllerKeyRemappingArg(&arg);
    HidLaControllerSupportArgPrivate privateArg{};
    privateArg.controller_support_caller = HidLaControllerSupportCaller_System;
    Result rc = setupControllerPrivateArg(
        privateArg,
        HidLaControllerSupportMode_ShowControllerKeyRemappingForSystem,
        sizeof(arg), false);
    // setup initializes individual fields but deliberately preserves caller.
    privateArg.controller_support_caller = HidLaControllerSupportCaller_System;
    if (R_SUCCEEDED(rc))
        rc = runControllerApplet("ControllerRemapping", privateArg, &arg, sizeof(arg));
    return rc;
}

static Result launchUserProfile(AccountUid uid) {
    FriendsLaArg current{};
    current.hdr.type = FriendsLaArgType_ShowMyProfile;
    current.hdr.uid = uid;

    FriendsLaArgV1 legacy{};
    const void* input = &current;
    size_t inputSize = sizeof(current);
    u32 version = 0x10000;
    if (hosversionBefore(9, 0, 0)) {
        legacy.hdr = current.hdr;
        legacy.data = current.data.common;
        input = &legacy;
        inputSize = sizeof(legacy);
        version = 0x1;
    }

    appletRequestToGetForeground();
    g_foregroundAppletActive = true;
    g_pendingForegroundAppletHome = false;
    const daemon::LibraryAppletInput appletInput{input, inputSize};
    const daemon::LibraryAppletRequest request{
        .id = AppletId_LibraryAppletMyPage,
        .name = "UserProfile",
        .version = version,
        .playStartupSound = true,
        .inputs = &appletInput,
        .inputCount = 1,
        .skipForegroundRestore = breezeSkipForegroundRestore,
        .terminateOnExit = breezeTerminateOnExit,
        .activeHolder = &g_breezeHolder,
    };
    const Result rc = daemon::runLibraryApplet(
        request, pumpBreezeAppletMessages,
        consumeForegroundAppletHomeRequest);
    drainBreezeLaunchMessages();
    g_foregroundAppletActive = false;
    return rc;
}

static void pushCommandResult(uint64_t requestId, Result result, const char* command) {
    const smi::CommandHeader response{
        smi::kCommandMagic,
        static_cast<uint32_t>(result),
        requestId,
    };
    AppletStorage storage{};
    Result rc = appletCreateStorage(&storage, sizeof(response));
    const bool created = R_SUCCEEDED(rc);
    if (created)
        rc = appletStorageWrite(&storage, 0, &response, sizeof(response));
    if (R_SUCCEEDED(rc)) {
        daemon::menu_la::pushStorage(&storage);
        return;
    }
    if (created)
        appletStorageClose(&storage);
    switchu::FileLog::log(
        "[smi] %s response create FAIL: 0x%X", command, rc);
}

static void handleMenuCommand() {
    if (!daemon::menu_la::isActive()) return;

    AppletStorage st;
    if (R_FAILED(daemon::menu_la::popStorage(&st))) return;

    smi::StorageReader reader(st);
    if (!reader.valid()) return;

    auto msg = reader.systemMessage();
    const uint64_t requestId = reader.requestId();
    switchu::FileLog::log("[smi] command=%u", (u32)msg);

    Result result = 0;

    switch (msg) {
    case smi::SystemMessage::LaunchApplication: {
        auto args = reader.pop<smi::LaunchAppArgs>();
        daemon::SystemAction action{};
        action.type = daemon::SystemActionType::LaunchApplication;
        action.requestId = requestId;
        action.titleId = args.title_id;
        std::memcpy(&action.uid, args.user_uid, sizeof(action.uid));
        result = g_actionQueue.enqueue(action);
        recordOperationResult(requestId, msg, result, args.title_id);
        switchu::FileLog::log("[smi] queued launch 0x%016lX (actions=%zu)", args.title_id, g_actionQueue.size());
        break;
    }

    case smi::SystemMessage::ResumeApplication:
        {
            daemon::SystemAction action{};
            action.type = daemon::SystemActionType::ResumeApplication;
            action.requestId = requestId;
            result = g_actionQueue.enqueue(action);
            recordOperationResult(requestId, msg, result,
                                  daemon::app::suspendedTitleId());
        }
        switchu::FileLog::logCommit("[smi] queued resume (actions=%zu)", g_actionQueue.size());
        break;

    case smi::SystemMessage::TerminateApplication:
        {
        const uint64_t terminatingTitleId = daemon::app::suspendedTitleId();
        result = daemon::app::terminate();
        recordOperationResult(requestId, msg, result, terminatingTitleId);
        if (R_SUCCEEDED(result)) {
            pushNotification(smi::MenuMessage::ApplicationExited);
        } else {
            pushNotification(smi::MenuMessage::OperationFailed,
                             terminatingTitleId,
                             static_cast<uint32_t>(result));
        }
        }
        break;

    case smi::SystemMessage::LaunchAlbum:
        {
            daemon::SystemAction action{};
            action.type = daemon::SystemActionType::OpenAlbum;
            action.requestId = requestId;
            result = g_actionQueue.enqueue(action);
            recordOperationResult(requestId, msg, result);
        }
        switchu::FileLog::log("[smi] queued album launch (actions=%zu)", g_actionQueue.size());
        break;

    case smi::SystemMessage::LaunchMiiEditor:
        {
            daemon::SystemAction action{};
            action.type = daemon::SystemActionType::OpenMiiEditor;
            action.requestId = requestId;
            result = g_actionQueue.enqueue(action);
            recordOperationResult(requestId, msg, result);
        }
        switchu::FileLog::log("[smi] queued Mii Editor launch (actions=%zu)", g_actionQueue.size());
        break;

    case smi::SystemMessage::LaunchNetConnect:
        {
            daemon::SystemAction action{};
            action.type = daemon::SystemActionType::OpenNetConnect;
            action.requestId = requestId;
            result = g_actionQueue.enqueue(action);
            recordOperationResult(requestId, msg, result);
        }
        switchu::FileLog::log("[smi] queued NetConnect launch (actions=%zu)", g_actionQueue.size());
        break;

    case smi::SystemMessage::LaunchUserPage: {
        auto args = reader.pop<smi::UserArgs>();
        daemon::SystemAction action{};
        action.type = daemon::SystemActionType::OpenUserPage;
        action.requestId = requestId;
        std::memcpy(&action.uid, args.user_uid, sizeof(action.uid));
        result = g_actionQueue.enqueue(action);
        recordOperationResult(requestId, msg, result);
        switchu::FileLog::log("[smi] queued User Page launch (actions=%zu)", g_actionQueue.size());
        break;
    }

    case smi::SystemMessage::LaunchUserCreator:
        {
            daemon::SystemAction action{};
            action.type = daemon::SystemActionType::OpenUserCreator;
            action.requestId = requestId;
            result = g_actionQueue.enqueue(action);
            recordOperationResult(requestId, msg, result);
        }
        switchu::FileLog::log("[smi] queued user creator launch (actions=%zu)",
                              g_actionQueue.size());
        break;

    case smi::SystemMessage::LaunchControllers:
        {
            daemon::SystemAction action{};
            action.type = daemon::SystemActionType::OpenControllers;
            action.requestId = requestId;
            result = g_actionQueue.enqueue(action);
            recordOperationResult(requestId, msg, result);
        }
        switchu::FileLog::log("[smi] queued Controller launch (actions=%zu)", g_actionQueue.size());
        break;

    case smi::SystemMessage::LaunchControllerRemapping:
        {
            daemon::SystemAction action{};
            action.type = daemon::SystemActionType::OpenControllerRemapping;
            action.requestId = requestId;
            result = g_actionQueue.enqueue(action);
            recordOperationResult(requestId, msg, result);
        }
        switchu::FileLog::log("[smi] queued controller remapping (actions=%zu)",
                              g_actionQueue.size());
        break;

    case smi::SystemMessage::EnterSleep:
        startPowerSequence("smi-sleep", smi::SystemMessage::EnterSleep);
        break;

    case smi::SystemMessage::Shutdown:
        startPowerSequence("smi-shutdown", smi::SystemMessage::Shutdown);
        break;

    case smi::SystemMessage::Reboot:
        startPowerSequence("smi-reboot", smi::SystemMessage::Reboot);
        break;

    case smi::SystemMessage::RequestForeground:
        appletRequestToGetForeground();
        break;

    case smi::SystemMessage::GetAppList: {
        break;
    }

    case smi::SystemMessage::GetSystemStatus: {
        auto status = buildSystemStatus();
        smi::StorageWriter writer((Result)0);
        writer.push(status);
        AppletStorage respSt;
        Result respRc = writer.createStorage(respSt);
        if (R_SUCCEEDED(respRc))
            daemon::menu_la::pushStorage(&respSt);
        else
            switchu::FileLog::log("[smi] GetSystemStatus response create FAIL: 0x%X", respRc);
        return;
    }

    case smi::SystemMessage::SetManualDateTime: {
        auto args = reader.pop<smi::ManualDateTimeArgs>();
        result = setManualDateTime(args);
        pushCommandResult(requestId, result, "SetManualDateTime");
        return;
    }

    case smi::SystemMessage::SetInternetTimeSync: {
        auto args = reader.pop<smi::InternetTimeSyncArgs>();
        result = setInternetTimeSync(args.enabled != 0);
        pushCommandResult(requestId, result, "SetInternetTimeSync");
        return;
    }

    case smi::SystemMessage::MenuReady:
        switchu::FileLog::log("[smi] menu ready");
        g_lastOperationFailure = {};
        g_batteryRefreshPending.store(true);
        shortenCatalogHold(kCatalogHoldAfterMenuReadyNs);
        break;

    case smi::SystemMessage::MenuClosing:
        switchu::FileLog::log("[smi] menu closing");
        break;

    }

}

static Result relaunchMenuAfterApplet(const char* appletName) {
    const bool returnToBreeze = g_breezeAppletReturn.take(g_remoteNroPending, g_launchRequestPending);
    if (g_remoteNroPending)
        return 0;
    if (returnToBreeze && breezeCanStart())
        return queueBreeze(g_breezeUid, appletName);
    if (!menuInstalled() && !g_launchRequestPending) {
        // No menu to return to: the Album closing with a game suspended goes
        // back to the game, as HOME in the SwitchU menu would.
        if (daemon::app::isRunning()) {
            const Result resumeRc = daemon::app::resume();
            switchu::FileLog::log("[action] %s closed, no menu; resume game rc=0x%X",
                                  appletName, resumeRc);
            if (R_SUCCEEDED(resumeRc))
                return 0;
        }
        return openHome(smi::MenuStartMode::AppletReturn, appletName);
    }
    if (g_launchRequestPending) {
        switchu::FileLog::log("[action] %s closed; menu skipped for requested launch",
                              appletName);
        return 0;
    }
    const auto status = buildSystemStatus();
    const Result rc = daemon::menu_la::launch(smi::MenuStartMode::AppletReturn, status);
    switchu::FileLog::log(
        "[action] %s menu restore rc=0x%X module=%u desc=%u running=%d suspended=0x%016lX",
        appletName, rc, R_MODULE(rc), R_DESCRIPTION(rc), status.app_running ? 1 : 0,
        status.suspended_app_id);
    return rc;
}

static void clearNroLaunchFiles() {
    ::remove(SWITCHU_NRO_REQUEST_PATH);
    ::remove(SWITCHU_NRO_ACK_PATH);
    ::remove(SWITCHU_NRO_ACK_TMP_PATH);
}

static bool writeNroLaunchInput(uint64_t requestId, const char* path) {
    SwitchUNroLaunchInput input{};
    input.magic = SWITCHU_NRO_LAUNCH_MAGIC;
    input.version = SWITCHU_NRO_LAUNCH_VERSION;
    input.requestId = requestId;
    if (!requestId || !path || std::strlen(path) >= sizeof(input.path))
        return false;
    std::snprintf(input.path, sizeof(input.path), "%s", path);
    clearNroLaunchFiles();
    bool written = false;
    if (FILE* file = std::fopen(SWITCHU_NRO_REQUEST_PATH, "wb")) {
        const bool complete = std::fwrite(&input, 1, sizeof(input), file) == sizeof(input);
        const int flushed = std::fflush(file);
        const int closed = std::fclose(file);
        written = complete && flushed == 0 && closed == 0 && commitSdCard("nro-launch-request");
    }
    if (!written)
        clearNroLaunchFiles();
    return written;
}

// Use the same bundled loader, but request the actual target explicitly.
// Its default/return path is hbmenu, never Breeze.
static Result launchNroInAlbum(const char* path, const char* name,
                               LibAppletMode mode = LibAppletMode_AllForeground) {
    if (mode == LibAppletMode_Background) {
        Handle handle = INVALID_HANDLE;
        Service* self = appletGetServiceSession_SelfController();
        serviceAssumeDomain(self);
        Result rc = serviceDispatch(self, 9,
            .out_handle_attrs = { SfOutHandleAttr_HipcCopy },
            .out_handles = &handle);
        if (R_FAILED(rc))
            return rc;
        Event launchable{};
        eventLoadRemote(&launchable, handle, false);
        rc = eventWait(&launchable, 0);
        eventClose(&launchable);
        // libnx otherwise waits UINT64_MAX here, including Background mode.
        if (R_FAILED(rc)) {
            switchu::FileLog::logCommit("[breeze-return] background not launchable rc=0x%X; using fallback", rc);
            return rc;
        }
    }
    Result rc = daemon::registerExternalContent(smi::kMenuTakeoverProgramId,
                                                smi::kBreezeAlbumLoaderDir);
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[breeze] Album loader registration FAIL: 0x%X", rc);
        return rc;
    }
    const uint64_t id = std::max<uint64_t>(1, armGetSystemTick());
    if (!writeNroLaunchInput(id, path)) {
        daemon::unregisterExternalContent(smi::kMenuTakeoverProgramId);
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }
    const u8 albumArg = AlbumLaArg_ShowAllAlbumFilesForHomeMenu;
    rc = launchLibraryApplet(AppletId_LibraryAppletPhotoViewer, name,
                             &albumArg, sizeof(albumArg), 0x10000, true, mode);
    clearNroLaunchFiles();
    const Result unregisterRc = daemon::unregisterExternalContent(smi::kMenuTakeoverProgramId);
    if (R_FAILED(unregisterRc))
        switchu::FileLog::log("[breeze] Album loader unregister FAIL: 0x%X", unregisterRc);
    return rc;
}

static void pumpRemoteNroApplet() {
    // nx-hbloader deliberately has no AM proxy. A one-shot receipt keeps the
    // target NRO's applet initialization and interactive channels untouched.
    if (!g_remoteNroAcked && (g_remoteNroLastProbeAt == 0 ||
        elapsedNsSince(g_remoteNroLastProbeAt) >= 250'000'000ULL)) {
        g_remoteNroLastProbeAt = armGetSystemTick();
        if (FILE* file = std::fopen(SWITCHU_NRO_ACK_PATH, "rb")) {
            SwitchUNroLaunchAck ack{};
            const bool valid = std::fread(&ack, 1, sizeof(ack), file) == sizeof(ack)
                && std::fgetc(file) == EOF;
            std::fclose(file);
            if (valid && ack.magic == SWITCHU_NRO_LAUNCH_MAGIC &&
                ack.version == SWITCHU_NRO_LAUNCH_VERSION && ack.requestId == g_remoteNroActiveId) {
                g_remoteNroAcked = true;
                g_remoteNroAckResult = ack.result;
                daemon::remote::finish(g_remoteNroActiveId, ack.result ? "failed" : "started", ack.result);
                switchu::FileLog::log("[remote-nro] loader handoff id=%lu rc=0x%X", g_remoteNroActiveId, ack.result);
                if (ack.result)
                    g_pendingForegroundAppletHome = true;
            }
        }
    }
    pumpBreezeAppletMessages();
    if (!g_remoteNroAcked && elapsedNsSince(g_remoteNroLoaderStartedAt) >= 10'000'000'000ULL) {
        g_remoteNroTimedOut = true;
        g_pendingForegroundAppletHome = true;
    }
}

static bool processRemoteNroRequest() {
    if (!g_remoteNroPending)
        return false;
    const auto fail = [](Result rc) {
        daemon::remote::finish(g_remoteNroRequest.id, "failed", rc);
        switchu::FileLog::log("[remote-nro] handoff failed id=%lu rc=0x%X", g_remoteNroRequest.id, rc);
        g_remoteNroPending = false;
        g_remoteNroForegroundRequested = false;
    };
    if (g_powerSequenceStarted.load() || g_launchRequestPending) {
        fail(daemon::SystemActionQueue::kBusyResult);
        return true;
    }
    if (g_foregroundAppletActive)
        return true;
    if (daemon::menu_la::hasHolder()) {
        const Result rc = daemon::menu_la::terminate(1'000'000'000ULL);
        if (R_FAILED(rc)) {
            fail(rc);
            return true;
        }
    }
    if (!g_daemonForegroundKnown) {
        if (!g_remoteNroForegroundRequested) {
            g_remoteNroForegroundRequested = true;
            g_remoteNroReceivedAt = armGetSystemTick();
            const Result rc = appletRequestToGetForeground();
            if (R_FAILED(rc)) {
                fail(rc);
                return true;
            }
            if (daemon::app::isRunning() && daemon::app::hasForeground())
                daemon::app::onHomeSuspend();
        } else if (elapsedNsSince(g_remoteNroReceivedAt) >= 5'000'000'000ULL) {
            fail(KERNELRESULT(TimedOut));
        }
        return true;
    }

    // The previous holder can restore qlaunch's foreground before this request
    // reaches the branch above. Its ChangeIntoForeground message then skips the
    // explicit request path, but the application model may still say Foreground
    // (notably after closing a Breeze overlay). Launching an AllForeground NRO
    // without synchronizing it makes the first HOME reclaim the NRO instead of
    // resuming the game. qlaunch is confirmed foreground here, so commit the
    // same application transition as the explicit foreground-request path.
    if (daemon::app::isRunning() && daemon::app::hasForeground()) {
        daemon::app::onHomeSuspend();
        switchu::FileLog::log("[remote-nro] synchronized suspended game before loader launch");
    }

    const auto request = g_remoteNroRequest;
    if (request.kind == daemon::remote::RequestKind::StartGame) {
        g_remoteNroPending = false;
        g_remoteNroForegroundRequested = false;
        if (daemon::app::isRunning()) {
            daemon::remote::finish(request.id, "failed", daemon::SystemActionQueue::kBusyResult);
            return true;
        }
        AccountUid uid{};
        g_launchRequestFromBreeze = true; // reuse the noninteractive first-account policy
        Result rc = chooseRequestedLaunchUser(request.titleId, &uid);
        g_launchRequestFromBreeze = false;
        if (R_SUCCEEDED(rc))
            rc = daemon::app::launch(request.titleId, uid);
        if (R_SUCCEEDED(rc)) {
            noteApplicationLaunched();
            scheduleBreezeReturnAfterGameStart();
        }
        daemon::remote::finish(request.id, R_FAILED(rc) ? "failed" : "started", rc);
        switchu::FileLog::logCommit("[remote-game] id=%lu title=0x%016lX rc=0x%X",
                                    request.id, request.titleId, rc);
        if (R_FAILED(rc))
            openHome(smi::MenuStartMode::MainMenu, "remote-game-failed");
        return true;
    }
    Result rc = daemon::registerExternalContent(smi::kMenuTakeoverProgramId, smi::kBreezeAlbumLoaderDir);
    if (R_FAILED(rc)) {
        fail(rc);
        return true;
    }
    if (!writeNroLaunchInput(request.id, request.path.c_str())) {
        ::remove(SWITCHU_NRO_REQUEST_PATH);
        daemon::unregisterExternalContent(smi::kMenuTakeoverProgramId);
        fail(MAKERESULT(Module_Libnx, LibnxError_BadInput));
        return true;
    }
    const u8 albumArg = AlbumLaArg_ShowAllAlbumFilesForHomeMenu;
    const daemon::LibraryAppletInput inputs[] = {{&albumArg, sizeof(albumArg)}};
    beginBreezeCandidate(false, g_breezeUid, "remote NRO loader");
    g_remoteNroPending = false;
    g_remoteNroForegroundRequested = false;
    g_remoteNroActiveId = request.id;
    g_remoteNroAcked = false;
    g_remoteNroAckResult = 0;
    g_remoteNroTimedOut = false;
    g_remoteNroHidden = false;
    g_remoteNroLoaderStartedAt = armGetSystemTick();
    g_remoteNroLastProbeAt = 0;
    g_foregroundAppletActive = true;
    g_pendingForegroundAppletHome = false;
    g_daemonForegroundKnown = false;
    daemon::LibraryAppletRequest launch{};
    launch.id = AppletId_LibraryAppletPhotoViewer;
    launch.name = "Remote NRO";
    launch.version = 0x10000;
    launch.inputs = inputs;
    launch.inputCount = 1;
    launch.activeHolder = &g_breezeHolder;
    launch.skipForegroundRestore = remoteNroSkipForegroundRestore;
    launch.terminateOnExit = breezeTerminateOnExit;
    rc = daemon::runLibraryApplet(launch, pumpRemoteNroApplet, consumeForegroundAppletHomeRequest);
    drainBreezeLaunchMessages();
    const bool gameStillForegroundBehindNro = g_remoteNroHidden &&
        daemon::app::isRunning() && daemon::app::hasForeground();
    g_foregroundAppletActive = false;
    ::remove(SWITCHU_NRO_REQUEST_PATH);
    ::remove(SWITCHU_NRO_ACK_PATH);
    ::remove(SWITCHU_NRO_ACK_TMP_PATH);
    const Result unregisterRc = daemon::unregisterExternalContent(smi::kMenuTakeoverProgramId);
    if (R_SUCCEEDED(rc) && R_FAILED(unregisterRc))
        rc = unregisterRc;
    if (g_remoteNroTimedOut || !g_remoteNroAcked)
        rc = R_FAILED(rc) ? rc : KERNELRESULT(TimedOut);
    if (R_SUCCEEDED(rc) && R_FAILED(g_remoteNroAckResult))
        rc = g_remoteNroAckResult;
    daemon::remote::finish(request.id, R_FAILED(rc) ? "failed" : "finished", rc);
    const bool handedOver = finishBreezeApplet("Remote NRO");
    g_remoteNroActiveId = 0;
    g_remoteNroHidden = false;
    if (!handedOver && !g_launchRequestPending && !gameStillForegroundBehindNro)
        relaunchMenuAfterApplet("Remote NRO");
    return true;
}

// Closing an explicitly opened Breeze never makes it the default HOME.
static void afterBreezeHomeClosed() {
    if (g_launchRequestPending) {
        switchu::FileLog::log("[breeze] closed; HOME skipped for requested launch");
        return;
    }
    relaunchMenuAfterApplet("Breeze");
}

static bool handleAction(daemon::SystemAction& action) {
    if (daemon::menu_la::hasHolder() || g_foregroundAppletActive)
        return false;

    switchu::FileLog::log("[action] handling type=%u", (u32)action.type);
    switch (action.type) {
        case daemon::SystemActionType::LaunchApplication: {
            if (accountUidIsValid(&action.uid))
                g_lastLaunchUid = action.uid;
            Result rc = daemon::app::launch(action.titleId, action.uid);
            recordOperationResult(action.requestId, smi::SystemMessage::LaunchApplication,
                                  rc, action.titleId);
            if (R_FAILED(rc)) {
                switchu::FileLog::log("[action] launch 0x%016lX FAIL: 0x%X", action.titleId, rc);
                openHome(smi::MenuStartMode::MainMenu, "action-launch");
            } else {
                noteApplicationLaunched();
            }
            return true;
        }

        case daemon::SystemActionType::ResumeApplication: {
            switchu::FileLog::logCommit("[action] resume: calling app::resume()");
            Result rc = daemon::app::resume();
            switchu::FileLog::logCommit("[action] resume: app::resume() -> 0x%X", rc);
            recordOperationResult(action.requestId, smi::SystemMessage::ResumeApplication,
                                  rc, daemon::app::suspendedTitleId());
            if (R_FAILED(rc)) {
                switchu::FileLog::log("[action] resume FAIL: 0x%X", rc);
                openHome(smi::MenuStartMode::MainMenu, "action-resume");
            }
            return true;
        }

        case daemon::SystemActionType::OpenHbmenu: {
            beginBreezeCandidate(false, AccountUid{}, "hbmenu NRO");
            const Result rc = launchNroInAlbum(SWITCHU_HBMENU_NRO_PATH, "Album(hbmenu)");
            if (R_FAILED(rc))
                switchu::FileLog::log("[action] hbmenu FAIL: 0x%X", rc);
            if (!finishBreezeApplet("hbmenu"))
                relaunchMenuAfterApplet("hbmenu");
            return true;
        }

        case daemon::SystemActionType::OpenAlbum: {
            const u8 albumArg = AlbumLaArg_ShowAllAlbumFilesForHomeMenu;
            beginBreezeCandidate(false, AccountUid{}, "Album");
            Result rc = launchLibraryApplet(AppletId_LibraryAppletPhotoViewer,
                                            "Album",
                                            &albumArg,
                                            sizeof(albumArg),
                                            0x10000,
                                            true);
            recordOperationResult(action.requestId, smi::SystemMessage::LaunchAlbum, rc);
            if (R_FAILED(rc))
                switchu::FileLog::log("[action] album FAIL: 0x%X", rc);
            if (!finishBreezeApplet("Album"))
                relaunchMenuAfterApplet("Album");
            return true;
        }

        case daemon::SystemActionType::OpenMiiEditor: {
            const auto miiVer = hosversionAtLeast(10, 2, 0) ? 0x4 : 0x3;
            const MiiLaAppletInput in = {
                .version = miiVer,
                .mode = MiiLaAppletMode_ShowMiiEdit,
                .special_key_code = MiiSpecialKeyCode_Normal,
            };
            Result rc = launchLibraryApplet(AppletId_LibraryAppletMiiEdit,
                                            "MiiEditor", &in, sizeof(in));
            recordOperationResult(action.requestId, smi::SystemMessage::LaunchMiiEditor, rc);
            if (R_FAILED(rc))
                switchu::FileLog::log("[action] Mii Editor FAIL: 0x%X", rc);
            relaunchMenuAfterApplet("MiiEditor");
            return true;
        }

        case daemon::SystemActionType::OpenControllers: {
            Result rc = launchControllerPairing();
            recordOperationResult(action.requestId, smi::SystemMessage::LaunchControllers, rc);
            if (R_FAILED(rc))
                switchu::FileLog::log("[action] Controllers FAIL: 0x%X", rc);
            relaunchMenuAfterApplet("Controllers");
            return true;
        }

        case daemon::SystemActionType::OpenControllerRemapping: {
            Result rc = launchControllerRemapping();
            recordOperationResult(action.requestId,
                                  smi::SystemMessage::LaunchControllerRemapping, rc);
            if (R_FAILED(rc))
                switchu::FileLog::log("[action] Controller remapping FAIL: 0x%X", rc);
            relaunchMenuAfterApplet("ControllerRemapping");
            return true;
        }

        case daemon::SystemActionType::OpenNetConnect: {
            const u32 netType = 1;
            Result rc = launchLibraryApplet(AppletId_LibraryAppletNetConnect,
                                            "NetConnect", &netType,
                                            sizeof(netType), 1);
            recordOperationResult(action.requestId, smi::SystemMessage::LaunchNetConnect, rc);
            if (R_FAILED(rc))
                switchu::FileLog::log("[action] NetConnect FAIL: 0x%X", rc);
            relaunchMenuAfterApplet("NetConnect");
            return true;
        }

        case daemon::SystemActionType::OpenUserPage: {
            beginBreezeCandidate(true, action.uid, "User Page");
            Result rc = launchUserProfile(action.uid);
            recordOperationResult(action.requestId, smi::SystemMessage::LaunchUserPage, rc);
            if (R_FAILED(rc))
                switchu::FileLog::log("[action] User Page FAIL: 0x%X", rc);
            if (!finishBreezeApplet("UserPage"))
                relaunchMenuAfterApplet("UserPage");
            return true;
        }

        case daemon::SystemActionType::OpenBreeze: {
            const bool automaticReturn = action.breezeReturnSessionId != 0;
            if (automaticReturn && (!daemon::app::isRunning() ||
                daemon::app::snapshot().sessionId != action.breezeReturnSessionId ||
                g_breezeRestoreTargetSessionId != action.breezeReturnSessionId)) {
                switchu::FileLog::log("[breeze-return] queued return discarded: application session changed");
                return true;
            }
            const AccountUid uid = resolveUser(action.uid);
            beginBreezeCandidate(true, uid, "explicit Breeze NRO");
            const bool background = automaticReturn &&
                g_breezeBackgroundFailedSessionId != action.breezeReturnSessionId;
            g_breezeStartedBackground = background;
            g_breezeHidden = background;
            switchu::FileLog::log("[breeze] opening Breeze via %s", g_breezeSlotName);
            const uint64_t startedAt = armGetSystemTick();
            g_breezeReturnAttemptSessionId = action.breezeReturnSessionId;
            g_breezeReturnAttemptStartedAt = startedAt;
            g_breezeReturnLastProbeAt = 0;
            g_breezeReturnAttemptTimedOut = false;
            const Result rc = launchNroInAlbum(smi::kBreezeNroPath, "Album(Breeze)",
                background ? LibAppletMode_Background : LibAppletMode_AllForeground);
            if (R_FAILED(rc))
                switchu::FileLog::log("[action] Breeze FAIL: 0x%X", rc);
            const bool handedOver = finishBreezeApplet("Breeze");
            g_breezeReturnAttemptSessionId = 0;
            g_breezeReturnAttemptTimedOut = false;
            if (automaticReturn && g_breezeRestoreTargetSessionId == action.breezeReturnSessionId) {
                if (background) {
                    g_breezeBackgroundFailedSessionId = action.breezeReturnSessionId;
                    switchu::FileLog::log("[breeze-return] background launch not ready; next attempt uses foreground fallback");
                }
                if (daemon::app::isRunning() && !daemon::app::hasForeground())
                    daemon::app::resume();
                retryBreezeReturn(action.breezeReturnSessionId, "loader closed without a Breeze running handshake");
                return true;
            }
            if (!handedOver)
                afterBreezeHomeClosed();
            return true;
        }

        case daemon::SystemActionType::OpenUserCreator: {
            switchu::FileLog::log("[applet] launching system user creator");
            appletRequestToGetForeground();
            g_foregroundAppletActive = true;
            g_pendingForegroundAppletHome = false;
            // pselShowUserCreator() first calls accountIsUserRegistrationRequestPermitted(),
            // which acc denies us and turns into 0x6159; drive the applet directly instead.
            PselUiSettings ui{};
            Result rc = pselUiCreate(&ui, PselUiMode_UserCreator);
            if (R_SUCCEEDED(rc))
                rc = pselUiShow(&ui, nullptr);
            if (rc == MAKERESULT(124, 1)) // nn::account::ResultCancelledByUser
                rc = 0;
            g_foregroundAppletActive = false;
            recordOperationResult(action.requestId,
                                  smi::SystemMessage::LaunchUserCreator, rc);
            if (R_FAILED(rc))
                switchu::FileLog::log("[action] user creator FAIL: 0x%X", rc);
            relaunchMenuAfterApplet("UserCreator");
            return true;
        }
    }

    return false;
}

static bool consumeOneAction() {
    if (g_actionQueue.empty() || daemon::menu_la::hasHolder() ||
        g_foregroundAppletActive)
        return false;

    daemon::SystemAction action{};
    if (!g_actionQueue.pop(action))
        return false;
    return handleAction(action);
}

static void mainLoop() {
    handleGeneralChannel();
    handleAppletMessages();
    pollRemoteNroRequests();
    handleMenuCommand();

    // SPSM terminates the process asynchronously. Do not schedule catalogue,
    // cache or logging work after the power request has begun.
    if (g_powerSequenceStarted.load())
        return;

    if (processRemoteNroRequest())
        return;

    if (g_pendingBreezeReturnSessionId != 0 &&
        elapsedNsSince(g_pendingHomeMenuStartedAt) >= 5'000'000'000ULL) {
        const uint64_t retrySessionId = g_pendingBreezeReturnSessionId;
        g_pendingBreezeReturnSessionId = 0;
        g_pendingHomeMenuLaunch = false;
        g_pendingHomeMenuSource = nullptr;
        g_pendingHomeMenuStartedAt = 0;
        if (daemon::app::isRunning() && !daemon::app::hasForeground())
            daemon::app::resume();
        retryBreezeReturn(retrySessionId, "foreground confirmation timed out");
    }

    bool didWork = false;

    if (g_eventRefreshPending.load() && shouldDeferViewPolling()) {
        g_eventPollCountdown = kViewPollIntervalTicks;
        g_eventPollsRemaining = 1;
    } else if (g_eventRefreshPending.exchange(false)) {
        if (!g_initialEventSkipped) {
            g_initialEventSkipped = true;
            g_appCatalogRefreshPending.store(false);
            clearCatalogHold();
            switchu::FileLog::log("[views] skipping initial catch-up event");
        } else {
            switchu::FileLog::log("[views] app record event — starting poll");
            g_eventPollCountdown  = kViewPollIntervalTicks;
            g_eventPollsRemaining = kViewPollAttempts;
        }
    }

    if (g_appCatalogRefreshPending.load() && !shouldDeferViewPolling() &&
        catalogHoldRemainingNs() == 0) {
        g_appCatalogRefreshPending.store(false);
        clearCatalogHold();
        bool catalogChanged = false;
        if (rebuildAppCatalog("record-event", &catalogChanged)) {
            if (catalogChanged) {
                if (daemon::menu_la::isActive())
                    pushNotification(smi::MenuMessage::AppRecordsChanged);
                else
                    g_catalogChangedWhileMenuAway.store(true);
            }
            didWork = true;
        }
    }
    if (g_controlCacheRefreshPending.load() && !controlCacheHoldActive() &&
        g_controlCacheRefreshPending.exchange(false)) {
        g_controlCacheHoldStartTick.store(0);
        if (daemon::menu_la::isActive())
            pushNotification(smi::MenuMessage::AppRecordsChanged);
        else
            g_catalogChangedWhileMenuAway.store(true);
        didWork = true;
    }
    if (g_eventPollsRemaining > 0 && shouldDeferViewPolling()) {
        g_eventPollCountdown = kViewPollIntervalTicks;
    } else if (g_eventPollsRemaining > 0 && --g_eventPollCountdown == 0) {
        bool needFullReload = sendViewFlagsUpdates();
        if (needFullReload) {
            if (daemon::menu_la::isActive())
                pushNotification(smi::MenuMessage::AppRecordsChanged);
            else
                g_catalogChangedWhileMenuAway.store(true);
            g_eventPollsRemaining = 0;
        } else {
            --g_eventPollsRemaining;
            if (g_eventPollsRemaining > 0)
                g_eventPollCountdown = kViewPollIntervalTicks;
        }
    }
    if (g_eventGcMountFailure.exchange(false)) {
        if (daemon::menu_la::isActive()) {
            pushNotification(smi::MenuMessage::GameCardMountFailure, 0,
                             (uint32_t)g_eventGcMountRc.load());
        }
    }
    if (g_batteryRefreshPending.exchange(false)) {
        pushBatteryStatusNotification(true);
    }

    if (g_catalogChangedWhileMenuAway.load() && daemon::menu_la::isActive()) {
        g_catalogChangedWhileMenuAway.store(false);
        switchu::FileLog::log(
            "[catalog] delivering coalesced refresh after menu relaunch");
        pushNotification(smi::MenuMessage::AppRecordsChanged);
        didWork = true;
    }

    if (g_menuRelaunchCooldown > 0)
        --g_menuRelaunchCooldown;

    if (daemon::menu_la::checkFinished()) {
        const uint64_t runtimeNs = daemon::menu_la::lastRuntimeNs();
        switchu::FileLog::log("[main] menu exited (reason=%d runtime=%lums)",
            (int)daemon::menu_la::exitReason(),
            static_cast<unsigned long>(runtimeNs / 1'000'000ULL));
        if (runtimeNs < 1'000'000'000ULL)
            ++g_menuFastExitCount;
        else
            g_menuFastExitCount = 0;
        if (g_menuFastExitCount >= 3) {
            g_menuRelaunchCooldown = 500;
            switchu::FileLog::log("[main] menu fast-exit guard active count=%d cooldown=%d",
                                  g_menuFastExitCount, g_menuRelaunchCooldown);
            g_menuFastExitCount = 0;
        }
        didWork = true;
    }

    didWork |= consumeOneAction();
    didWork |= consumeLaunchRequest();

    if (daemon::app::checkFinished()) {
        switchu::FileLog::log("[main] app exited");
        cancelBreezeReturn("game exited");
        g_breezeRelaunchPending = false;
        if (g_pendingHomeMenuLaunch) {
            switchu::FileLog::log("[main] clearing pending HOME foreground handoff: app exited");
            g_pendingHomeMenuLaunch = false;
            g_pendingHomeMenuSource = nullptr;
            g_pendingHomeMenuStartedAt = 0;
            g_pendingBreezeReturnSessionId = 0;
        }
        if (daemon::menu_la::isActive()) {
            pushNotification(smi::MenuMessage::ApplicationExited);
        } else if (!g_launchRequestPending) {
            openHome(smi::MenuStartMode::MainMenu, "app-exited");
        }
        didWork = true;
    }

    didWork |= restoreBreezeAfterLaunch();

    if (!didWork && g_menuRelaunchCooldown <= 0 && g_actionQueue.empty() &&
        !g_launchRequestPending && !daemon::app::isRunning() && !daemon::menu_la::hasHolder() &&
        !g_foregroundAppletActive) {
        switchu::FileLog::log("[main] no app/menu active; opening HOME");
        openHome(smi::MenuStartMode::MainMenu, "idle");
    }
}

static bool mainLoopNeedsFastTick() {
    if (g_powerSequenceStarted.load())
        return false;
    if (g_remoteNroPending || daemon::remote::hasPending())
        return true;
    if (shouldDeferViewPolling()) {
        return g_menuRelaunchCooldown > 0 || !g_actionQueue.empty() ||
               g_breezeLaunchHandoff.pending();
    }
    return g_eventPollsRemaining > 0
        || g_appCatalogRefreshPending.load()
        || g_controlCacheRefreshPending.load()
        || g_menuRelaunchCooldown > 0
        || g_launchRequestPending
        || g_breezeLaunchHandoff.pending()
        || !g_actionQueue.empty();
}

static void waitForMainWork() {
    Waiter waiters[6]{};
    s32 waiterCount = 0;

    if (Event* messageEvent = appletGetMessageEvent())
        waiters[waiterCount++] = waiterForEvent(messageEvent);
    if (g_generalChannelEventReady)
        waiters[waiterCount++] = waiterForEvent(&g_generalChannelEvent);

    waiters[waiterCount++] = waiterForUEvent(&g_mainWakeEvent);

    if (Event* commandEvent = daemon::menu_la::commandEvent())
        waiters[waiterCount++] = waiterForEvent(commandEvent);
    if (Event* menuStateEvent = daemon::menu_la::stateChangedEvent())
        waiters[waiterCount++] = waiterForEvent(menuStateEvent);
    if (Event* appStateEvent = daemon::app::stateChangedEvent())
        waiters[waiterCount++] = waiterForEvent(appStateEvent);

    if (waiterCount == 0) {
        svcSleepThread(1'000'000'000ULL);
        return;
    }

    s32 signalledIndex = -1;
    const u64 timeout = mainLoopNeedsFastTick()
        ? 10'000'000ULL
        : 1'000'000'000ULL;
    static Result s_lastWaitFailure = 0;
    static uint32_t s_waitFailureRepeatCount = 0;
    const Result rc = waitObjects(&signalledIndex, waiters, waiterCount, timeout);
    if (R_FAILED(rc) && rc != KERNELRESULT(TimedOut)) {
        if (rc == s_lastWaitFailure) {
            ++s_waitFailureRepeatCount;
        } else {
            s_lastWaitFailure = rc;
            s_waitFailureRepeatCount = 1;
        }
        if (s_waitFailureRepeatCount <= 3 || (s_waitFailureRepeatCount % 120) == 0) {
            switchu::FileLog::log("[main] waitObjects FAIL: 0x%X count=%u waiters=%d",
                                  rc,
                                  (unsigned)s_waitFailureRepeatCount,
                                  (int)waiterCount);
        }
        // Applet holder or applet-manager events can be rejected by waitObjects
        // while foreground ownership is changing. The next main-loop tick polls
        // all holders and channels again, so keep a conservative backoff instead
        // of turning an invalid waiter into a hot loop.
        svcSleepThread(100'000'000ULL);
    } else {
        s_lastWaitFailure = 0;
        s_waitFailureRepeatCount = 0;
    }
}


static Thread g_eventThread = {};
static std::atomic<bool> g_eventRunning{false};

static void eventManagerThreadFunc(void* arg) {
    (void)arg;
    switchu::FileLog::log("[event] thread alive");

    Event recordEvent = {};
    Result rc = nsGetApplicationRecordUpdateSystemEvent(&recordEvent);
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[event] nsGetApplicationRecordUpdateSystemEvent FAIL: 0x%X", rc);
        return;
    }
    switchu::FileLog::log("[event] registered ApplicationRecordUpdateSystemEvent");

    Event gcMountFailEvent = {};
    bool hasGcEvent = false;
    if (hosversionAtLeast(3, 0, 0)) {
        rc = nsGetGameCardMountFailureEvent(&gcMountFailEvent);
        if (R_SUCCEEDED(rc)) {
            hasGcEvent = true;
            switchu::FileLog::log("[event] registered GameCardMountFailureEvent");
        } else {
            switchu::FileLog::log("[event] nsGetGameCardMountFailureEvent FAIL: 0x%X", rc);
        }
    } else {
        switchu::FileLog::log("[event] GameCardMountFailureEvent not supported on this firmware");
    }

    PsmSession psmSession{};
    bool hasPsmEvent = false;
    if (g_psmReady) {
        rc = psmBindStateChangeEvent(&psmSession, true, true, true);
        if (R_SUCCEEDED(rc)) {
            hasPsmEvent = true;
            switchu::FileLog::log("[event] registered PSM state change event");
        } else {
            switchu::FileLog::log("[event] psmBindStateChangeEvent FAIL: 0x%X", rc);
        }
    } else {
        switchu::FileLog::log("[event] PSM unavailable; battery events disabled");
    }

    while (g_eventRunning.load()) {
        s32 evIdx = -1;
        Result waitRc;
        if (hasGcEvent && hasPsmEvent) {
            waitRc = waitMulti(&evIdx, 1'000'000'000ULL,
                waiterForEvent(&recordEvent),
                waiterForEvent(&gcMountFailEvent),
                waiterForEvent(&psmSession.StateChangeEvent));
        } else if (hasGcEvent) {
            waitRc = waitMulti(&evIdx, 1'000'000'000ULL,
                waiterForEvent(&recordEvent),
                waiterForEvent(&gcMountFailEvent));
        } else if (hasPsmEvent) {
            waitRc = waitMulti(&evIdx, 1'000'000'000ULL,
                waiterForEvent(&recordEvent),
                waiterForEvent(&psmSession.StateChangeEvent));
        } else {
            waitRc = waitMulti(&evIdx, 1'000'000'000ULL,
                waiterForEvent(&recordEvent));
        }

        if (waitRc == KERNELRESULT(TimedOut)) continue;
        if (R_FAILED(waitRc)) continue;

        if (evIdx == 0) {
            eventClear(&recordEvent);
            switchu::FileLog::log("[event] ApplicationRecordUpdateSystemEvent fired");

            g_appCatalogRefreshPending.store(true);
            g_eventRefreshPending.store(true);
            ueventSignal(&g_mainWakeEvent);
        } else if (evIdx == 1 && hasGcEvent) {
            eventClear(&gcMountFailEvent);

            Result failRc = switchu::ns::getLastGameCardMountFailure();
            switchu::FileLog::log("[event] GameCardMountFailure rc=0x%X", failRc);

            g_eventGcMountRc.store(failRc);
            g_eventGcMountFailure.store(true);
            ueventSignal(&g_mainWakeEvent);
        } else if ((hasGcEvent && hasPsmEvent && evIdx == 2) ||
                   (!hasGcEvent && hasPsmEvent && evIdx == 1)) {
            eventClear(&psmSession.StateChangeEvent);
            switchu::FileLog::log("[event] PSM state change fired");
            g_batteryRefreshPending.store(true);
            ueventSignal(&g_mainWakeEvent);
        }

        svcSleepThread(100'000ULL);
    }

    eventClose(&recordEvent);
    if (hasGcEvent) eventClose(&gcMountFailEvent);
    if (hasPsmEvent) psmUnbindStateChangeEvent(&psmSession);
    switchu::FileLog::log("[event] thread exiting");
}

static Result startEventManager() {
    g_eventRunning.store(true);
    Result rc = threadCreate(&g_eventThread, eventManagerThreadFunc, nullptr,
                             nullptr, 0x4000, 0x2C, -2);
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[event] threadCreate FAIL: 0x%X", rc);
        return rc;
    }
    rc = threadStart(&g_eventThread);
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[event] threadStart FAIL: 0x%X", rc);
        threadClose(&g_eventThread);
        return rc;
    }
    switchu::FileLog::log("[event] thread started");
    return 0;
}

static void stopEventManager() {
    g_eventRunning.store(false);
    threadWaitForExit(&g_eventThread);
    threadClose(&g_eventThread);
    switchu::FileLog::log("[event] thread stopped");
}

static Thread g_controlCacheThread = {};
static std::atomic<bool> g_controlCacheRunning{false};
static bool g_controlCacheStarted = false;

static void controlCacheThreadFunc(void* arg) {
    (void)arg;
    switchu::FileLog::log("[control-cache] thread alive");
    switchu::control_cache::ensureDirectory();

    while (g_controlCacheRunning.load()) {
        // Control-data reads also write icon/meta files. Defer them while a
        // game owns the foreground so they cannot contend with LayeredFS I/O.
        if (shouldDeferViewPolling()) {
            svcSleepThread(500'000'000ULL);
            continue;
        }

        uint64_t titleId = 0;
        int attempts = 0;
        if (!popControlCacheTitle(titleId, attempts)) {
            waitSingle(waiterForUEvent(&g_controlCacheWakeEvent), UINT64_MAX);
            continue;
        }

        if (titleId == 0 || switchu::control_cache::hasUsableCache(titleId))
            continue;

        auto* controlData = new NsApplicationControlData();
        if (!controlData) {
            switchu::FileLog::log("[control-cache] alloc FAIL title=0x%016lX", titleId);
            svcSleepThread(250'000'000ULL);
            continue;
        }

        size_t controlSize = 0;
        const uint64_t startTick = armGetSystemTick();
        Result rc = nsGetApplicationControlData(NsApplicationControlSource_Storage,
                                                titleId,
                                                controlData,
                                                sizeof(*controlData),
                                                &controlSize);
        // Switch 2 Edition titles can leave the legacy control slot without a
        // name; theirs sits in ACD slots 1..3, reachable only through
        // ControlData2. Keep the legacy data if no slot has a name either.
        if (R_FAILED(rc) || controlSize < sizeof(NacpStruct)
            || !switchu::control_cache::hasTitleName(*controlData)) {
            auto* alternate = new NsApplicationControlData();
            for (u8 acdIndex = 1; alternate && acdIndex <= 3; ++acdIndex) {
                u64 alternateSize = 0;
                u32 unk = 0;
                Result alternateRc = nsGetApplicationControlData2(NsApplicationControlSource_Storage,
                                                                  titleId,
                                                                  alternate,
                                                                  sizeof(*alternate),
                                                                  0,
                                                                  acdIndex,
                                                                  &alternateSize,
                                                                  &unk);
                if (R_SUCCEEDED(alternateRc) && alternateSize >= sizeof(NacpStruct)
                    && switchu::control_cache::hasTitleName(*alternate)) {
                    std::swap(controlData, alternate);
                    controlSize = alternateSize;
                    rc = alternateRc;
                    switchu::FileLog::log("[control-cache] 0x%016lX name from acd slot %u",
                                          titleId, acdIndex);
                    break;
                }
            }
            delete alternate;
        }
        const uint64_t elapsedMs = armTicksToNs(armGetSystemTick() - startTick) / 1'000'000ULL;
        if (R_SUCCEEDED(rc) && controlSize >= sizeof(NacpStruct)) {
            const bool ok = switchu::control_cache::writeFromControlData(
                titleId,
                *controlData,
                controlSize);
            switchu::FileLog::log("[control-cache] cached 0x%016lX size=%zu elapsed=%lums ok=%d",
                                  titleId,
                                  controlSize,
                                  static_cast<unsigned long>(elapsedMs),
                                  ok ? 1 : 0);
            if (ok) {
                if (!g_controlCacheRefreshPending.exchange(true))
                    g_controlCacheHoldStartTick.store(armGetSystemTick());
                ueventSignal(&g_mainWakeEvent);
            } else {
                requeueControlCacheTitle(titleId, attempts);
            }
        } else {
            switchu::FileLog::log("[control-cache] GetControlData FAIL title=0x%016lX rc=0x%X size=%zu elapsed=%lums",
                                  titleId,
                                  rc,
                                  controlSize,
                                  static_cast<unsigned long>(elapsedMs));
            requeueControlCacheTitle(titleId, attempts);
        }

        delete controlData;
        svcSleepThread(10'000'000ULL);
    }

    switchu::FileLog::log("[control-cache] thread exiting");
}

static Result startControlCacheWorker() {
    g_controlCacheRunning.store(true);
    Result rc = threadCreate(&g_controlCacheThread, controlCacheThreadFunc, nullptr,
                             nullptr, 0x10000, 0x2D, -2);
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[control-cache] threadCreate FAIL: 0x%X", rc);
        return rc;
    }

    rc = threadStart(&g_controlCacheThread);
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[control-cache] threadStart FAIL: 0x%X", rc);
        threadClose(&g_controlCacheThread);
        return rc;
    }

    switchu::FileLog::log("[control-cache] thread started");
    g_controlCacheStarted = true;
    return 0;
}

static void stopControlCacheWorker() {
    if (!g_controlCacheStarted)
        return;
    g_controlCacheRunning.store(false);
    ueventSignal(&g_controlCacheWakeEvent);
    threadWaitForExit(&g_controlCacheThread);
    threadClose(&g_controlCacheThread);
    g_controlCacheStarted = false;
    switchu::FileLog::log("[control-cache] thread stopped");
}

int main(int argc, char* argv[]) {
    switchu::FileLog::log("[daemon] main() entry");
    std::error_code configError;
    std::filesystem::create_directories("sdmc:/config/SwitchU", configError);
    switchu::FileLog::log("[daemon] Remote NRO channel v5: port=6802 loader=one-shot boot=bundled input=home,sticks controller=transient capture=jpeg home=retain-nro logs=logs/SwitchU");

    ueventCreate(&g_mainWakeEvent, true);
    ueventCreate(&g_controlCacheWakeEvent, true);
    // A reboot or interrupted handoff must never replay a previous request.
    ::remove(SWITCHU_NRO_REQUEST_PATH);
    ::remove(SWITCHU_NRO_ACK_PATH);
    ::remove(SWITCHU_NRO_ACK_TMP_PATH);
    daemon::remote::start([] { ueventSignal(&g_mainWakeEvent); });
    Result generalEventRc = appletGetPopFromGeneralChannelEvent(&g_generalChannelEvent);
    g_generalChannelEventReady = R_SUCCEEDED(generalEventRc);
    if (R_FAILED(generalEventRc))
        switchu::FileLog::log("[daemon] general channel event unavailable: 0x%X", generalEventRc);

    appletLoadAndApplyIdlePolicySettings();

    rebuildAppCatalog("boot");

    Result rc = startControlCacheWorker();
    if (R_FAILED(rc))
        switchu::FileLog::log("[daemon] control cache worker failed: 0x%X (non-fatal)", rc);

    rc = startEventManager();
    if (R_FAILED(rc))
        switchu::FileLog::log("[daemon] event manager failed: 0x%X (non-fatal)", rc);

    ::remove(smi::kBreezeRunningFlag);
    destroyLeftoverOverlayLayers("boot");
    {
        mkdir("sdmc:/config", 0777);
        mkdir("sdmc:/config/SwitchU", 0777);
        if (FILE* marker = fopen(smi::kHomeToggleForkMarker, "wb")) {
            fputs("home_toggle\n", marker);
            fclose(marker);
        } else {
            switchu::FileLog::log("[daemon] fork marker write failed");
        }
    }
    switchu::FileLog::log("[daemon] opening HOME (legacy_breeze_first_ignored=%d menu=%d breeze=%d)",
                          breezeFlagExists(smi::kBreezeFirstFlag) ? 1 : 0,
                          menuInstalled() ? 1 : 0, breezeCanStart() ? 1 : 0);
    rc = openHome(smi::MenuStartMode::StartupBoot, "boot");
    if (R_FAILED(rc))
        switchu::FileLog::log("[daemon] HOME launch failed: 0x%X", rc);

    while (g_running.load()) {
        mainLoop();
        waitForMainWork();
    }

    daemon::remote::stop();
    stopEventManager();
    stopControlCacheWorker();
    daemon::menu_la::terminate();
    daemon::app::cleanup();
    if (g_generalChannelEventReady) {
        eventClose(&g_generalChannelEvent);
        g_generalChannelEventReady = false;
    }
    switchu::FileLog::log("[daemon] shutdown complete");
    return 0;
}
