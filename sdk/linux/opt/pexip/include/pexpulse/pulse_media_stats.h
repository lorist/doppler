/* Pexip Pulse library.
 *
 * Copyright (C) 2022 Pexip AS
 * @author Knut Saastad
 * @author Tulio Beloqui
 */

#ifndef _PULSE_MEDIA_STATS_H_
#define _PULSE_MEDIA_STATS_H_

#include "pulse_types.h"

PULSE_DECL_BEGIN

/**
 * @brief Retrieve a snapshot of media statistics.
 * @param client         The Pulse handle.
 * @param window_size    The statistics window size in seconds.
 * @param conceal_resets Whether to conceal counter resets in the statistics.
 * @return A pointer to a new PulseMediaStats instance, or NULL on failure.
 */
PULSE_EXPORT
PulseMediaStats * pulse_media_stats_get (Pulse * client, uint32_t window_size, bool conceal_resets);

/**
 * @brief Free a PulseMediaStats instance previously obtained from pulse_media_stats_get.
 * @param stats  The PulseMediaStats instance to free.
 */
PULSE_EXPORT
void pulse_media_stats_free (PulseMediaStats * stats);

PULSE_DECL_END

#endif /* _PULSE_MEDIA_STATS_H_ */