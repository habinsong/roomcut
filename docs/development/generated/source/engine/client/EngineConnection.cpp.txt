#include "EngineConnection.hpp"
#include "roomcut_ipc.h"
#include <bootstrap.h>

namespace roomcut {

EngineConnection::SendRight::~SendRight() {
    if (MACH_PORT_VALID(port)) mach_port_deallocate(mach_task_self(), port);
}

EngineConnection::Lease EngineConnection::acquire() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!cached_) {
        // Allocate ownership before acquiring a kernel right, so an allocation
        // failure cannot leak the right returned by the resolver.
        auto right = std::make_shared<SendRight>();
        right->port = lookup_(context_);
        if (!MACH_PORT_VALID(right->port)) return {};
        cached_ = std::move(right);
    }
    return Lease(cached_);
}

void EngineConnection::invalidate(const Lease& failed) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Object identity also distinguishes two resolutions of the same Mach
    // name. An older failed request must not clear a replacement connection.
    if (cached_ == failed.right_) cached_.reset();
}

mach_port_t EngineConnection::lookupService(void*) {
    const auto task = mach_task_self();
    mach_port_t bootstrap = MACH_PORT_NULL;
    if (task_get_bootstrap_port(task, &bootstrap) != KERN_SUCCESS || !MACH_PORT_VALID(bootstrap))
        return MACH_PORT_NULL;
    mach_port_t service = MACH_PORT_NULL;
    const auto result = bootstrap_look_up(bootstrap, ROOMCUT_MACH_SERVICE_NAME, &service);
    mach_port_deallocate(task, bootstrap);
    if (result != KERN_SUCCESS) {
        if (MACH_PORT_VALID(service)) mach_port_deallocate(task, service);
        return MACH_PORT_NULL;
    }
    return service;
}
} // namespace roomcut
