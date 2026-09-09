#ifndef ROOMCUT_SERVICE_PORT_HPP
#define ROOMCUT_SERVICE_PORT_HPP

#include <mach/mach.h>

namespace roomcut {

// The engine's Mach service name, and the receive right behind it, for as long
// as the process serves it. launchd owns that right in production; the dev path
// registers one so a driver simulator in the same session can look it up. Only a
// right this object created is released — never one launchd handed over.
class ServicePort {
public:
    ServicePort() = default;
    ~ServicePort() { release(); }
    ServicePort(const ServicePort&) = delete;
    ServicePort& operator=(const ServicePort&) = delete;

    // Claims `name`: launchd check-in first, then the dev registration. Logs
    // which path it took. Returns false with no right held on failure.
    bool acquire(const char* name);
    void release();

    mach_port_t port() const { return port_; }
    bool valid() const { return port_ != MACH_PORT_NULL; }
    // True when this process created the right (dev path) and must give it back.
    bool devRegistered() const { return owned_; }

private:
    mach_port_t port_ = MACH_PORT_NULL;
    bool owned_ = false;
};

} // namespace roomcut
#endif
