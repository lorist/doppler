/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2026 Pexip AS
 */

#ifndef _PULSE_RTP_SESSION_H_
#define _PULSE_RTP_SESSION_H_

#include <stdint.h>

#include "pulse_error.h"
#include "pulse_media.h"

/*
  PULSE RTP SESSION

  A simple RTP video output: send H.264 video for a given content slot
  (MAIN or PRESENTATION) to a remote (host, video_port). RTCP is sent
  to video_port + 1. No ICE, no DTLS, no audio.
*/

PULSE_DECL_BEGIN

/**
 * @brief RTP video output session configuration.
 * Specifies the remote endpoint for an outbound RTP video stream.
 */
typedef struct _PulseRtpVideoOutputConfig
{
  const char * host; /* Remote host as an IPv4/IPv6 string. */
  uint16_t port;     /* Remote RTP port. RTCP uses port + 1. */
} PulseRtpVideoOutputConfig;

/**
 * @brief pulse_rtp_session_connect_video_output
 * Connects an RTP video output session for the specified media content.
 * If a session is already active for the content slot, it will be
 * disconnected first and replaced by the new configuration.
 * @param client The Pulse handle.
 * @param media_content The media content slot to use (MAIN or PRESENTATION).
 * @param config The RTP video output configuration.
 * @return PULSE_SUCCESS on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_rtp_session_connect_video_output (Pulse * client, PulseMediaContent media_content,
                                                   const PulseRtpVideoOutputConfig * config);

/**
 * @brief pulse_rtp_session_disconnect_video_output
 * Disconnects the RTP video output session for the specified media content.
 * @param client The Pulse handle.
 * @param media_content The media content slot to disconnect.
 * @return PULSE_SUCCESS on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_rtp_session_disconnect_video_output (Pulse * client, PulseMediaContent media_content);

PULSE_DECL_END

#endif /* _PULSE_RTP_SESSION_H_ */
