#include "Control.hpp"
#include "Heartbeat.hpp"
#include "RoomcutTransport.h"
#include <cstdio>

using namespace roomcut;
static int failures = 0;
#define CHECK(condition, message) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); ++failures; } } while (0)

static unsigned temporaryRights() {
    mach_port_name_array_t names = nullptr;
    mach_port_type_array_t types = nullptr;
    mach_msg_type_number_t nameCount = 0, typeCount = 0;
    CHECK(mach_port_names(mach_task_self(), &names, &nameCount, &types, &typeCount) == KERN_SUCCESS, "list task rights");
    unsigned count = 0;
    for (unsigned i = 0; i < typeCount; ++i)
        if (types[i] & (MACH_PORT_TYPE_SEND_ONCE | MACH_PORT_TYPE_DEAD_NAME)) ++count;
    if (names) vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(names), nameCount * sizeof(*names));
    if (types) vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(types), typeCount * sizeof(*types));
    return count;
}

int main() {
    for (int client = 0; client < 3; ++client) {
        mach_port_t service = MACH_PORT_NULL;
        CHECK(mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &service) == KERN_SUCCESS, "allocate service");
        CHECK(mach_port_insert_right(mach_task_self(), service, service, MACH_MSG_TYPE_MAKE_SEND) == KERN_SUCCESS, "own service right");
        mach_port_limits_t limits{1};
        CHECK(mach_port_set_attributes(mach_task_self(), service, MACH_PORT_LIMITS_INFO,
            reinterpret_cast<mach_port_info_t>(&limits), MACH_PORT_LIMITS_INFO_COUNT) == KERN_SUCCESS, "set queue limit");
        mach_msg_header_t queued{};
        queued.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
        queued.msgh_remote_port = service;
        CHECK(mach_msg(&queued, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(queued), 0,
            MACH_PORT_NULL, 0, MACH_PORT_NULL) == KERN_SUCCESS, "fill peer queue");
        const auto before = temporaryRights();
        for (int attempt = 0; attempt < 10; ++attempt) {
            uint32_t peerState = 123;
            RoomcutStateReply state{};
            state.state = 123;
            kern_return_t result;
            if (client == 0) result = heartbeatProbe(service, attempt, 1, &peerState);
            else if (client == 1) result = Roomcut_TransportProbePort(service, attempt, 1);
            else result = controlGetState(service, 1, &state);
            CHECK(result == MACH_SEND_TIMED_OUT, "full queue produces send timeout");
            CHECK(peerState == 123 && state.state == 123, "failed request preserves outputs");
        }
        mach_port_urefs_t refs = 0;
        mach_port_get_refs(mach_task_self(), service, MACH_PORT_RIGHT_SEND, &refs);
        std::printf("client=%d remaining service refs=%u temporary rights delta=%d\n",
                    client, refs, static_cast<int>(temporaryRights()) - static_cast<int>(before));
        CHECK(refs == 1, "repeated timeouts keep exactly the original service right");
        CHECK(temporaryRights() == before, "repeated timeouts leave no reply or dead-name rights");
        if (refs) mach_port_mod_refs(mach_task_self(), service, MACH_PORT_RIGHT_SEND, -refs);
        mach_port_mod_refs(mach_task_self(), service, MACH_PORT_RIGHT_RECEIVE, -1);
    }
    if (!failures) std::puts("all Mach timeout cleanup tests passed");
    return failures ? 1 : 0;
}
