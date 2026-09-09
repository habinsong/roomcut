#include "ServicePort.hpp"

#include <cstdio>
#include <servers/bootstrap.h>

namespace roomcut {

bool ServicePort::acquire(const char* name) {
    release();

    mach_port_t bootstrapPort = MACH_PORT_NULL;
    if (task_get_bootstrap_port(mach_task_self(), &bootstrapPort) != KERN_SUCCESS ||
        bootstrapPort == MACH_PORT_NULL) {
        std::fprintf(stderr, "[engine] no bootstrap port\n");
        return false;
    }

    // Production path: launchd already owns the receive right; claim it.
    mach_port_t service = MACH_PORT_NULL;
    kern_return_t kr = bootstrap_check_in(bootstrapPort, name, &service);
    if (kr == KERN_SUCCESS && service != MACH_PORT_NULL) {
        std::fprintf(stderr, "[engine] bootstrap_check_in OK (launchd-provided)\n");
        port_ = service;
        owned_ = false;
        return true;
    }
    std::fprintf(stderr, "[engine] bootstrap_check_in failed (%s); trying dev register\n",
                 bootstrap_strerror(kr));

    // Dev path: allocate a receive right + a send right and register the name so
    // a driver-sim in the same session can bootstrap_look_up it. bootstrap_register
    // is deprecated (launchd MachServices check-in is the production path) but
    // still works in a login session for local testing, so suppress the warning
    // for this one intentional dev-only call.
    const mach_port_t self = mach_task_self();
    if (mach_port_allocate(self, MACH_PORT_RIGHT_RECEIVE, &service) != KERN_SUCCESS) {
        return false;
    }
    if (mach_port_insert_right(self, service, service, MACH_MSG_TYPE_MAKE_SEND) != KERN_SUCCESS) {
        mach_port_mod_refs(self, service, MACH_PORT_RIGHT_RECEIVE, -1);
        return false;
    }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    kr = bootstrap_register(bootstrapPort, const_cast<char*>(name), service);
#pragma clang diagnostic pop
    if (kr != KERN_SUCCESS) {
        std::fprintf(stderr, "[engine] bootstrap_register failed (%s)\n", bootstrap_strerror(kr));
        mach_port_mod_refs(self, service, MACH_PORT_RIGHT_RECEIVE, -1);
        return false;
    }
    std::fprintf(stderr, "[engine] bootstrap_register OK (dev path)\n");
    port_ = service;
    owned_ = true;
    return true;
}

void ServicePort::release() {
    if (owned_ && port_ != MACH_PORT_NULL) {
        mach_port_mod_refs(mach_task_self(), port_, MACH_PORT_RIGHT_RECEIVE, -1);
    }
    port_ = MACH_PORT_NULL;
    owned_ = false;
}

} // namespace roomcut
