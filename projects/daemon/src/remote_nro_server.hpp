#pragma once

#include <cstdint>
#include <string>

namespace switchu::daemon::remote {

constexpr const char* kRemoteChannelVersionMarker = "remote channel v3";

struct Request {
    std::uint64_t id = 0;
    std::string path;
};

// Starts the authenticated BreezePC listener. Returns false when disabled,
// misconfigured, or unable to create the service thread. The service retries
// socket setup in the background when the network is temporarily unavailable.
bool start(void (*wake)());
void stop();

// take() transfers the queued request and changes its state to "launching".
bool take(Request& request);
bool hasPending();
bool busy();

// "started" permits a later explicit replacement; queued/launching stay busy.
// A stale request's completion cannot overwrite a newer request's receipt.
void finish(std::uint64_t id, const char* state, std::uint32_t rc = 0);

} // namespace switchu::daemon::remote
