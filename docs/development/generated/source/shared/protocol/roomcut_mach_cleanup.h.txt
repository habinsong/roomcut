#ifndef ROOMCUT_MACH_CLEANUP_H
#define ROOMCUT_MACH_CLEANUP_H

#include <mach/mach.h>

/* Recoverable send errors return rights through a pseudo-receive. The local
 * reply right is not covered by mach_msg_destroy; COPY_SEND rights that were
 * returned as MOVE_SEND must also be released without dropping our original.
 * Other errors may mean the kernel already partially destroyed the message. */
static inline void roomcut_destroy_unsent_message(mach_msg_header_t* header, mach_msg_return_t result) {
    if (result != MACH_SEND_INVALID_DEST && result != MACH_SEND_TIMED_OUT && result != MACH_SEND_INTERRUPTED)
        return;
    const mach_msg_type_name_t localType = MACH_MSGH_BITS_LOCAL(header->msgh_bits);
    if (MACH_PORT_VALID(header->msgh_local_port) &&
        (localType == MACH_MSG_TYPE_MOVE_SEND || localType == MACH_MSG_TYPE_MOVE_SEND_ONCE)) {
        mach_port_deallocate(mach_task_self(), header->msgh_local_port);
        header->msgh_local_port = MACH_PORT_NULL;
    }
    mach_msg_destroy(header);
}

#endif
