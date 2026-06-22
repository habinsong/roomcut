/*
 * test_heartbeat.cpp — verifies the HEALTH_CHECK liveness path
 * (DEVELOPMENT_PLAN.md §4.1 heartbeat, §4.2 timeout → peer lost).
 *
 *   1. Live peer: a responder thread echoes one probe with its state; the
 *      prober gets KERN_SUCCESS and reads that state back.
 *   2. Dead peer: probing a health port with no responder times out
 *      (MACH_RCV_TIMED_OUT) — this is exactly the signal the driver uses to
 *      transition Streaming → EngineLost.
 */
#include "Heartbeat.hpp"

#include <cstdio>
#include <cstring>
#include <pthread.h>

#include <mach/mach.h>

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); g_failures++; } \
} while (0)

using namespace roomcut;

struct RespCtx {
    mach_port_t healthPort;
    uint32_t    state;
    bool        ready;
    pthread_mutex_t mtx;
    pthread_cond_t  cv;
};

// Responder: receive one probe, reply with its state, exit.
static void* responderThread(void* arg) {
    RespCtx* c = (RespCtx*)arg;
    RoomcutHealthRequest req;
    kern_return_t kr = heartbeatReceive(c->healthPort, 2000, &req);
    CHECK(kr == KERN_SUCCESS, "responder receives probe");
    if (kr == KERN_SUCCESS) {
        kr = heartbeatRespond(req, c->state);
        CHECK(kr == KERN_SUCCESS, "responder sends reply");
    }
    return nullptr;
}

static void test_live_peer() {
    mach_port_t self = mach_task_self();
    mach_port_t health = MACH_PORT_NULL;
    CHECK(mach_port_allocate(self, MACH_PORT_RIGHT_RECEIVE, &health) == KERN_SUCCESS, "alloc health port");
    CHECK(mach_port_insert_right(self, health, health, MACH_MSG_TYPE_MAKE_SEND) == KERN_SUCCESS, "insert send right");

    RespCtx c;
    std::memset(&c, 0, sizeof(c));
    c.healthPort = health;
    c.state = ROOMCUT_STATE_RUNNING;

    pthread_t t;
    pthread_create(&t, nullptr, responderThread, &c);

    uint32_t peerState = 0xFFFFFFFF;
    kern_return_t kr = heartbeatProbe(health, 42, 2000, &peerState);
    CHECK(kr == KERN_SUCCESS, "live probe succeeds");
    CHECK(peerState == ROOMCUT_STATE_RUNNING, "probe reads peer state");

    pthread_join(t, nullptr);
    mach_port_mod_refs(self, health, MACH_PORT_RIGHT_RECEIVE, -1);
}

static void test_dead_peer() {
    mach_port_t self = mach_task_self();
    mach_port_t health = MACH_PORT_NULL;
    CHECK(mach_port_allocate(self, MACH_PORT_RIGHT_RECEIVE, &health) == KERN_SUCCESS, "alloc dead health port");
    CHECK(mach_port_insert_right(self, health, health, MACH_MSG_TYPE_MAKE_SEND) == KERN_SUCCESS, "insert send right (dead)");

    // No responder: the probe sends fine but no reply arrives → timeout.
    uint32_t peerState = 0;
    kern_return_t kr = heartbeatProbe(health, 7, 200, &peerState);
    CHECK(kr == MACH_RCV_TIMED_OUT, "dead probe times out (peer lost signal)");

    mach_port_mod_refs(self, health, MACH_PORT_RIGHT_RECEIVE, -1);
}

int main() {
    test_live_peer();
    test_dead_peer();

    if (g_failures == 0) { printf("all heartbeat tests passed\n"); return 0; }
    fprintf(stderr, "%d heartbeat check(s) failed\n", g_failures);
    return 1;
}
