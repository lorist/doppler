/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2022 Pexip AS
 * @author Knut Saastad
 * @author Tulio Beloqui
 */

#ifndef _PULSE_DEBUG_H_
#define _PULSE_DEBUG_H_

#include "pulse_error.h"
#include "pulse_media.h"

PULSE_DECL_BEGIN

/**
 * @brief Start the lip-sync test sender.
 * @param client  The Pulse handle.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_connect_lipsync_test_send (Pulse * client);

/**
 * @brief Start the lip-sync test receiver.
 * @param client  The Pulse handle.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_connect_lipsync_test_recv (Pulse * client);

/**
 * @brief Display the resulting mixed video on Linux using xvimagesink.
 * @param client         The Pulse handle.
 * @param media_content  The media content to preview.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_preview_video (Pulse * client, PulseMediaContent media_content);

PULSE_DECL_END

#endif /* _PULSE_DEBUG_H_ */