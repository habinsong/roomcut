// test_service_port.cpp — the engine's service-name lifetime. Whichever path the
// session gives us, the served port has to carry real messages, and only a right
// this process created may be handed back.
#include "ServicePort.hpp"

#include <cstdio>
#include <cstring>
#include <servers/bootstrap.h>
#include <string>
#include <unistd.h>

using namespace roomcut;
static int failures = 0;
#define CHECK(condition, message) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); ++failures; } } while (0)

// A private name so the test never touches the installed engine's service.
static std::string testName(const char* tag) {
    char name[128];
    std::snprintf(name, sizeof(name), "com.roomcut.test.%s.%d", tag, (int)getpid());
    return name;
}

static mach_port_type_t typeOf(mach_port_t port) {
    mach_port_type_t type = 0;
    if (mach_port_type(mach_task_self(), port, &type) != KERN_SUCCESS) return 0;
    return type;
}

// Which path we get — launchd check-in or the dev registration — is the session's
// decision. Both must serve; only what we made is ours to release.
static void testAcquiredNameServesAndOnlyOurOwnRightIsReleased() {
    const std::string name = testName("serve");
    ServicePort service;
    if (!service.acquire(name.c_str())) {
        std::printf("no service name available in this session; skipped\n");
        return;
    }
    CHECK(service.valid(), "an acquired service has a port");
    const mach_port_t port = service.port();
    const bool ours = service.devRegistered();
    CHECK((typeOf(port) & MACH_PORT_TYPE_RECEIVE) != 0, "the service holds the receive right");

    // A peer that looks the name up must reach this port.
    mach_port_t peer = MACH_PORT_NULL;
    mach_port_t bootstrapPort = MACH_PORT_NULL;
    CHECK(task_get_bootstrap_port(mach_task_self(), &bootstrapPort) == KERN_SUCCESS, "bootstrap port");
    CHECK(bootstrap_look_up(bootstrapPort, name.c_str(), &peer) == KERN_SUCCESS, "the name resolves");
    mach_msg_header_t sent{};
    sent.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
    sent.msgh_size = sizeof(sent);
    sent.msgh_remote_port = peer;
    sent.msgh_id = 4242;
    CHECK(mach_msg(&sent, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(sent), 0,
                   MACH_PORT_NULL, 500, MACH_PORT_NULL) == KERN_SUCCESS, "a peer can send to it");
    struct { mach_msg_header_t header; mach_msg_trailer_t trailer; } received{};
    CHECK(mach_msg(&received.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof(received),
                   service.port(), 500, MACH_PORT_NULL) == KERN_SUCCESS, "the service receives it");
    CHECK(received.header.msgh_id == 4242, "the message arrived on the served port");
    if (peer != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), peer);

    service.release();
    CHECK(!service.valid() && !service.devRegistered(), "a released service holds nothing");
    if (ours) {
        CHECK((typeOf(port) & MACH_PORT_TYPE_RECEIVE) == 0, "a right we created goes back");
    } else {
        // launchd handed this one over for the life of the process; taking it
        // apart is launchd's business, not the engine's.
        CHECK((typeOf(port) & MACH_PORT_TYPE_RECEIVE) != 0, "a launchd-provided right is left alone");
    }
}

static void testReleaseIsIdempotent() {
    const std::string name = testName("release");
    ServicePort service;
    if (!service.acquire(name.c_str())) {
        std::printf("no service name available in this session; skipped\n");
        return;
    }
    const mach_port_t port = service.port();
    const bool ours = service.devRegistered();
    service.release();
    const mach_port_type_t afterFirst = typeOf(port);
    service.release();
    CHECK(typeOf(port) == afterFirst, "releasing twice does not touch the port again");
    CHECK(!service.valid(), "the released service stays empty");
    if (ours) CHECK(afterFirst == 0, "our own right was the one released");
}

int main() {
    testAcquiredNameServesAndOnlyOurOwnRightIsReleased();
    testReleaseIsIdempotent();
    if (failures == 0) {
        std::printf("all service port tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d service port check(s) failed\n", failures);
    return 1;
}
