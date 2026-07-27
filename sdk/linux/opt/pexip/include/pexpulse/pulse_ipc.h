/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2022 Pexip AS
 * @author Knut Saastad
 * @author Tulio Beloqui
 */

#ifndef _PULSE_IPC_H_
#define _PULSE_IPC_H_

#include <stddef.h>
#include "pulse_config.h"
#include "pulse_error.h"
#include "pulse_types.h"

PULSE_DECL_BEGIN

/**
 * @brief Create a new IPC handle.
 * @param name  Name identifying the IPC channel.
 * @param size  Buffer size in bytes for the IPC channel.
 * @return A pointer to a new PulseIPCHandle, or NULL on failure.
 */
PULSE_EXPORT
PulseIPCHandle * pulse_ipc_new (const char * name, uint32_t size);

/**
 * @brief Free an IPC handle previously created with pulse_ipc_new.
 * @param handle  The IPC handle to free.
 */
PULSE_EXPORT
void pulse_ipc_free (PulseIPCHandle * handle);

/**
 * @brief Write a line of data to the named IPC channel.
 * @param name  Name identifying the IPC channel.
 * @param data  Null-terminated string to write.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_ipc_write_line (const char * name, const char * data);

/**
 * @brief Read a line of data from an IPC channel.
 * @param handle      The IPC handle.
 * @param[out] line_out  Pointer to receive the read line.
 * @param[out] len_out   Pointer to receive the length of the read line.
 * @param wakeup_ms   Wakeup interval in milliseconds while waiting for data.
 * @param timeout_ms  Maximum time in milliseconds to wait for data.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_ipc_read_line (PulseIPCHandle * handle, char ** line_out, uint32_t * len_out, uint32_t wakeup_ms,
                                uint32_t timeout_ms);

/**
 * @brief Cancel a pending read operation on an IPC channel.
 * @param handle  The IPC handle.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_ipc_cancel_read (PulseIPCHandle * handle);

PULSE_DECL_END

#endif /* _PULSE_IPC_H_ */
