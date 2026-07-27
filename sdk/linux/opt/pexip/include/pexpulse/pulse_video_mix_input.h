/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2026 Pexip AS
 * @author Tulio Beloqui
 */

#ifndef _PULSE_VIDEO_MIX_INPUT_H_
#define _PULSE_VIDEO_MIX_INPUT_H_

#include "pulse_error.h"
#include "pulse_media.h"
PULSE_DECL_BEGIN
/* Lifecycle — called from pulse_init / pulse_dispose */
void pulse_video_mix_input_init (Pulse * client);
void pulse_video_mix_input_clear (Pulse * client);

/**
 * @brief Acquire: resolve a device source into a PulseVideoMixInputID.
 * @param client         The Pulse handle.
 * @param device         The device as input.
 * @param[out] out_id    The resolved VideoMixInputID.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_video_mix_input_from_device (Pulse * client, const PulseDevice * device,
                                              PulseVideoMixInputID * out_id);

/**
 * @brief Acquire: resolve a desktop source into a PulseVideoMixInputID.
 * @param client         The Pulse handle.
 * @param handle         Desktop handle.
 * @param desktop_type   Desktop type.
 * @param[out] out_id    The resolved VideoMixInputID.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_video_mix_input_from_desktop (Pulse * client, uint64_t handle, PulseDesktopVideoType desktop_type,
                                               PulseVideoMixInputID * out_id);

/**
 * @brief Acquire: resolve a file source into a PulseVideoMixInputID.
 * @param client         The Pulse handle.
 * @param file_path      The image file as input.
 * @param[out] out_id    The resolved VideoMixInputID.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_video_mix_input_from_file (Pulse * client, const char * file_path, PulseVideoMixInputID * out_id);

/**
 * @brief Acquire: resolve a file source into a PulseVideoMixInputID, with explicit loop control.
 *
 * Equivalent to pulse_video_mix_input_from_file() for image files (the
 * @p loop parameter is ignored). For video files (e.g. MP4) the session
 * is configured before its first start so playback either loops
 * indefinitely (@p loop == true) or plays once and stops at EOS
 * (@p loop == false). The legacy pulse_video_mix_input_from_file()
 * leaves the underlying mp4bin's loop-count at its compiled-in default,
 * which is intentionally preserved for backwards-compatibility with
 * existing callers and tests.
 *
 * @param client         The Pulse handle.
 * @param file_path      Path to the media file (image or video).
 * @param loop           For video files: loop forever (true) or play
 *                       once (false). Ignored for image files.
 * @param[out] out_id    The resolved VideoMixInputID.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_video_mix_input_from_file_with_loop (Pulse * client, const char * file_path, bool loop,
                                                      PulseVideoMixInputID * out_id);

/**
 * @brief Acquire: resolve a previously connected data session video input into a PulseVideoMixInputID.
 *
 * The caller must have previously connected a data session with video caps
 * via pulse_data_session_connect_input(). The caller
 * remains responsible for the data session lifetime and for pushing frames
 * via pulse_data_session_push_frame().
 *
 * @param client         The Pulse handle.
 * @param media_content  The media content slot whose data session video input
 *                       should be used (e.g. PULSE_MEDIA_CONTENT_MAIN).
 * @param out_id         [out] The resolved PulseVideoMixInputID.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_video_mix_input_from_data_session (Pulse * client, PulseMediaContent media_content,
                                                    PulseVideoMixInputID * out_id);

/**
 * @brief Acquire: create a transparent annotation overlay and resolve
 *        it into a PulseVideoMixInputID.
 *
 * The returned input is an additive transparent video source whose
 * frames are produced by Pulse's annotation (drawing) subsystem —
 * see pulse_annotation.h for the drawing primitives. Typical usage
 * is to add it to the same PulseVideoMixConfig as a camera / desktop /
 * file input, on a higher PulseVideoMixInput::layer index, so the
 * mixer composites the strokes on top of the underlying video.
 *
 * Handle equivalence: the returned PulseVideoMixInputID may be passed
 * directly as the PulseAnnotationID argument to any pulse_annotation_*
 * function — the two handle types share the same numeric space.
 *
 * @param client       The Pulse handle.
 * @param width        Canvas width in pixels (> 0).
 * @param height       Canvas height in pixels (> 0).
 * @param[out] out_id  The resolved PulseVideoMixInputID.
 * @return PULSE_SUCCESS on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_video_mix_input_from_annotation (Pulse * client, uint32_t width, uint32_t height,
                                                  PulseVideoMixInputID * out_id);

/**
 * @brief Acquire: resolve a previously connected RTMP input session video input into a PulseVideoMixInputID.
 *
 * The caller must have previously connected an RTMP input session with video support
 * via pulse_rtmp_session_connect_input(). The caller
 * remains responsible for the RTMP session lifetime.
 *
 * @param client         The Pulse handle.
 * @param media_content  The media content slot whose RTMP input session video input
 *                       should be used (e.g. PULSE_MEDIA_CONTENT_MAIN).
 *                       PULSE_MEDIA_CONTENT_SELFVIEW is not supported and will
 *                       return PULSE_ERROR_INVALID_MEDIA_CONTENT_TYPE.
 * @param out_id         [out] The resolved PulseVideoMixInputID.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_video_mix_input_from_rtmp_session (Pulse * client, PulseMediaContent media_content,
                                                    PulseVideoMixInputID * out_id);

/**
 * @brief Acquire: resolve a previously connected RTSP input session video input into a PulseVideoMixInputID.
 *
 * The caller must have previously connected an RTSP input session with video support
 * via pulse_rtsp_session_connect_input(). The caller
 * remains responsible for the RTSP session lifetime.
 *
 * The same #PulseRtspSessionID may be acquired into multiple mix
 * surfaces (or even multiple times into the same mix) — the RTSP
 * frames are fanned out to every consumer.
 *
 * @param client         The Pulse handle.
 * @param session_id     The RTSP session handle returned by
 *                       pulse_rtsp_session_connect_input(). Must refer
 *                       to a session that has at least one video input.
 * @param out_id         [out] The resolved PulseVideoMixInputID.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_video_mix_input_from_rtsp_session (Pulse * client, PulseRtspSessionID session_id,
                                                    PulseVideoMixInputID * out_id);

/**
 * @brief Acquire: resolve a single stream of a previously connected RTSP input
 * session into a PulseVideoMixInputID.
 *
 * Use this in preference to pulse_video_mix_input_from_rtsp_session() when the
 * camera advertises more than one stream of a given media type and the caller
 * wants to pick a specific one (typically driven by a UI dropdown populated
 * from pulse_rtsp_session_stream_iterator_new()). The "session-only" variant
 * always resolves to the first video stream announced by the camera, which is
 * fine for single-stream IP cameras but not for multi-channel/NVR sources or
 * cameras that expose both a high-resolution main stream and a low-resolution
 * sub-stream.
 *
 * The same #PulseRtspStreamID may be acquired into multiple mix surfaces;
 * the underlying RTP frames are fanned out to every consumer.
 *
 * Once the owning session is disconnected, the resolved input becomes
 * stale — subsequent mixer lookups will resolve to "no input" and the
 * caller should release the entry.
 *
 * @param client      The Pulse handle.
 * @param session_id  The RTSP session handle returned by
 *                    pulse_rtsp_session_connect_input().
 * @param stream_id   A stream id obtained via pulse_rtsp_stream_get_id() on
 *                    a #PulseRtspStream returned by an iterator created with
 *                    pulse_rtsp_session_stream_iterator_new(@c session_id,
 *                    #PULSE_MEDIA_VIDEO, ...). Must refer to a video stream
 *                    of @c session_id.
 * @param out_id      [out] The resolved PulseVideoMixInputID.
 * @return PULSE_SUCCESS on success, or a PulseError code on failure
 *   (notably #PULSE_ERROR_NOT_CONFIGURED if the session/stream is unknown,
 *   or #PULSE_ERROR_INVALID_MEDIA_TYPE if @c stream_id is an audio stream).
 */
PULSE_EXPORT
PulseError pulse_video_mix_input_from_rtsp_stream (Pulse * client, PulseRtspSessionID session_id,
                                                   PulseRtspStreamID stream_id, PulseVideoMixInputID * out_id);

/**
 * @brief Release: tear down what we created, free the entry
 *
 * @param client        The Pulse handle.
 * @param id            The PulseVideoMixInputID to release.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_video_mix_input_release (Pulse * client, PulseVideoMixInputID id);
PULSE_DECL_END

#endif /* _PULSE_VIDEO_MIX_INPUT_H_ */