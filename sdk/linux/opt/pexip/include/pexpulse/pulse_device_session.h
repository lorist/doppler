/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2022 Pexip AS
 * @author Knut Saastad
 * @author Tulio Beloqui
 */

#ifndef _PULSE_DEVICE_SESSION_H_
#define _PULSE_DEVICE_SESSION_H_

#include "pulse_error.h"

/*
  PULSE DEVICE SESSION
*/

PULSE_DECL_BEGIN

PULSE_EXPORT
PulseError pulse_device_session_connect_system_default (Pulse * client, PulseMediaContent media_content,
                                                        PulseMediaType media_type, PulseMediaDirection media_direction);

PULSE_EXPORT
PulseError pulse_device_session_connect_device_by_id (Pulse * client, PulseDeviceID deviceID, PulseMediaType media_type,
                                                      PulseMediaDirection media_direction,
                                                      PulseMediaContent content_type);

PULSE_EXPORT
PulseError pulse_device_session_connect_device (Pulse * client, const PulseDevice * device,
                                                PulseMediaContent content_type);

PULSE_EXPORT
PulseError pulse_device_session_is_connected (Pulse * client, const PulseDevice * device,
                                              PulseMediaContent content_type, bool * is_connected);

PULSE_EXPORT
PulseError pulse_device_session_is_connected_by_id (Pulse * client, PulseDeviceID deviceID, PulseMediaType media_type,
                                                    PulseMediaDirection media_direction, PulseMediaContent content_type,
                                                    bool * is_connected);

/* Releases the video devices (camera or window ouptut) previously connected for MAIN VIDEO */
PULSE_EXPORT
PulseError pulse_device_session_disconnect_main_video (Pulse * client, PulseMediaContent media_content,
                                                       PulseMediaDirection media_direction);

/* Releases the audio devices (microphone and speaker) previously connected for MAIN AUDIO */
PULSE_EXPORT
PulseError pulse_device_session_disconnect_main_audio (Pulse * client);

/* Control device PTZ - only supported on Linux for now */
PULSE_EXPORT
PulseError pulse_device_session_control_ptz (Pulse * client, PulseMediaContent media_content,
                                             PulseFeccActionType action,
                                             PulseFeccMovementDirection movement_direction_mask, uint32_t timeout_ms);

/* Get device PTZ values directly - only supported on Linux for now */
/* Pan, tilt and zoom values are configured as floats in the range from 0.0 to 1.0  */
PULSE_EXPORT
PulseError pulse_device_session_get_ptz_values (Pulse * client, PulseMediaContent media_content, float * pan,
                                                float * tilt, float * zoom);

/* Set device PTZ values directly - only supported on Linux for now */
/* Pan, tilt and zoom values are configured as floats in the range from 0.0 to 1.0  */
PULSE_EXPORT
PulseError pulse_device_session_set_ptz_values (Pulse * client, PulseMediaContent media_content, float pan, float tilt,
                                                float zoom);

/* Reset device PTZ - only supported on Linux for now */
PULSE_EXPORT
PulseError pulse_device_session_reset_ptz (Pulse * client, PulseMediaContent media_content, PulsePTZAxis reset_axis);

PULSE_EXPORT
PulseError pulse_device_session_connect_desktop_audio_input (Pulse * client, uint64_t pid);

PULSE_EXPORT
PulseError pulse_device_session_disconnect_desktop_audio_input (Pulse * client);

PULSE_EXPORT
PulseError pulse_device_session_connect_desktop_video_input (Pulse * client, uint64_t handle,
                                                             PulseDesktopVideoType type);

PULSE_EXPORT
PulseError pulse_device_session_disconnect_desktop_video_input (Pulse * client);

/**
 * @brief Creates a new video handle for the given media content.
 * For Windows this returns a IDXGISwapChain1*.
 *
 * Call pulse_device_session_release_video_handle() to release the handle after its not used.
 *
 * @param client The Pulse handle
 * @param media_content the media content associated to the surface
 * @param width the desired width for the surface
 * @param height the desired height for the surface
 * @param color the background color in ARGB32, example for black: 0xff000000
 * @return a *new* video handle for that media_content.
 */
PULSE_EXPORT
void * pulse_device_session_create_video_handle (Pulse * client, PulseMediaContent media_content, int width, int height,
                                                 uint32_t color);

/**
 * @brief Resizes a video handle.
 *
 *
 * @param client The Pulse handle
 * @param handle The video handle to resize
 * @param width the desired width for the surface
 * @param height the desired height for the surface
 * @return a PulseError
 */
PULSE_EXPORT
PulseError pulse_device_session_resize_video_handle (Pulse * client, void * handle, int width, int height);

/**
 * @brief Release a video handle
 * @param client The Pulse handle
 * @param handle The video handle to release
 * @return a PulseError indicating success or failure
 */
PULSE_EXPORT
PulseError pulse_device_session_release_video_handle (Pulse * client, void * handle);

PULSE_DECL_END

#endif /* _PULSE_DEVICE_SESSION_H_ */