/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2022 Pexip AS
 * @author Knut Saastad
 * @author Tulio Beloqui
 */

#ifndef _PULSE_FILE_SESSION_H_
#define _PULSE_FILE_SESSION_H_

#include "pulse_error.h"
#include "pulse_media.h"

PULSE_DECL_BEGIN

/**
 * @brief Connect a file input session for playback.
 * @param client         The Pulse handle.
 * @param file_format    The format of the media file.
 * @param file_path      Path to the media file.
 * @param media_content  The media content type (audio/video).
 * @return PULSE_SUCCESS (0) on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_file_input_session_connect (Pulse * client, PulseMediaFileFormat file_format, const char * file_path,
                                             PulseMediaContent media_content);

/**
 * @brief Disconnect a file input session.
 * @param client         The Pulse handle.
 * @param media_content  The media content type to disconnect.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_file_input_session_disconnect (Pulse * client, PulseMediaContent media_content);

/**
 * @brief Pause a file input session.
 * @param client         The Pulse handle.
 * @param media_content  The media content type to pause.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_file_input_session_pause (Pulse * client, PulseMediaContent media_content);

/**
 * @brief Resume a paused file input session.
 * @param client         The Pulse handle.
 * @param media_content  The media content type to resume.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_file_input_session_resume (Pulse * client, PulseMediaContent media_content);

/**
 * @brief Set the loop mode for a file input session.
 * @param client         The Pulse handle.
 * @param media_content  The media content type.
 * @param loop           Whether to loop playback.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_file_input_session_loop (Pulse * client, PulseMediaContent media_content, bool loop);

PULSE_DECL_END

#endif /* _PULSE_FILE_SESSION_H_ */