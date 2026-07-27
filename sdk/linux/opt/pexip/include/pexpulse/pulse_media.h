/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2022 Pexip AS
 * @author Knut Saastad
 * @author Tulio Beloqui
 */

#include <stdbool.h>

#include "pulse_config.h"

#ifndef _PULSE_MEDIA_H_
#define _PULSE_MEDIA_H_

PULSE_DECL_BEGIN

/**
 * @brief Media stream types.
 * Defines whether a media stream carries audio or video.
 */
typedef enum _PulseMediaType
{
  PULSE_MEDIA_AUDIO = 0,
  PULSE_MEDIA_VIDEO
} PulseMediaType;

/**
 * @brief Media stream direction types.
 * Defines whether a media stream is an input (source) or output (sink).
 */
typedef enum _PulseMediaDirection
{
  PULSE_MEDIA_INPUT = 0,
  PULSE_MEDIA_OUTPUT
} PulseMediaDirection;

/**
 * @brief Media content types.
 * Defines the role of a media stream (main, presentation, selfview, etc.).
 */
typedef enum _PulseMediaContent
{
  PULSE_MEDIA_CONTENT_MAIN = 0,
  PULSE_MEDIA_CONTENT_PRESENTATION,
  PULSE_MEDIA_CONTENT_SELFVIEW,
  PULSE_MEDIA_CONTENT_QUAD_SPLIT_0,
  PULSE_MEDIA_CONTENT_QUAD_SPLIT_1,
  PULSE_MEDIA_CONTENT_QUAD_SPLIT_2,
  PULSE_MEDIA_CONTENT_QUAD_SPLIT_3,
  PULSE_MEDIA_CONTENT_PREFLIGHT,
  __PULSE_MEDIA_CONTENT_MAX,
} PulseMediaContent;

/**
 * @brief Media session types.
 * Defines the underlying session type used for a media stream.
 */
typedef enum _PulseMediaSessionType
{
  PULSE_MEDIA_SESSION_DEVICE = 0,
  PULSE_MEDIA_SESSION_FILE,
  PULSE_MEDIA_SESSION_DATA,
  PULSE_MEDIA_SESSION_RTMP,
  PULSE_MEDIA_SESSION_RTSP,
  PULSE_MEDIA_SESSION_PEXCISION,
  PULSE_MEDIA_SESSION_MIXING_GROUP,
  PULSE_MEDIA_SESSION_DEVICE_DESKTOP,
  PULSE_MEDIA_SESSION_GFX,
  PULSE_MEDIA_SESSION_VIDEO_MIX,
  PULSE_MEDIA_SESSION_RTP,
  __PULSE_MEDIA_SESSION_MAX,
} PulseMediaSessionType;

/**
 * @brief Media file format types.
 * Defines supported file formats for media recording or playback.
 */
typedef enum _PulseMediaFileFormat
{
  PULSE_MEDIA_FILE_FORMAT_MP4 = 0
} PulseMediaFileFormat;

/**
 * @brief Video pixel format types.
 * Defines the pixel formats supported for raw video frames.
 */
typedef enum _PulseMediaPixelFormat
{
  PULSE_MEDIA_PIXEL_FORMAT_NV12 = 0,
  PULSE_MEDIA_PIXEL_FORMAT_I420,
  PULSE_MEDIA_PIXEL_FORMAT_RGB,
  PULSE_MEDIA_PIXEL_FORMAT_RGBA,
} PulseMediaPixelFormat;

/**
 * @brief Audio sample format types.
 * Defines the sample formats supported for raw audio frames.
 */
typedef enum _PulseMediaAudioFormat
{
  PULSE_MEDIA_AUDIO_FORMAT_S16LE = 0,
  PULSE_MEDIA_AUDIO_FORMAT_F32LE,
} PulseMediaAudioFormat;

/**
 * @brief Audio channel layout types.
 * Defines the channel layout for multi-channel audio.
 */
typedef enum _PulseMediaAudioLayout
{
  PULSE_MEDIA_AUDIO_LAYOUT_INTERLEAVED = 0,
  PULSE_MEDIA_AUDIO_LAYOUT_NON_INTERLEAVED
} PulseMediaAudioLayout;

/**
 * @brief Video rotation types.
 * Defines the clockwise rotation applied to a video frame.
 */
typedef enum _PulseMediaRotation
{
  PULSE_MEDIA_ROTATION_0 = 0,
  PULSE_MEDIA_ROTATION_90,
  PULSE_MEDIA_ROTATION_180,
  PULSE_MEDIA_ROTATION_270,
} PulseMediaRotation;

/**
 * @brief pulse_media_type_to_string
 * Converts a PulseMediaType to its string representation.
 * @param media_type The media type to convert.
 * @param upper_case If true, returns the string in upper case.
 * @return A string representation of the media type.
 */
PULSE_EXPORT
const char * pulse_media_type_to_string (PulseMediaType media_type, bool upper_case);

/**
 * @brief pulse_media_direction_to_string
 * Converts a PulseMediaDirection to its string representation.
 * @param direction The media direction to convert.
 * @return A string representation of the media direction.
 */
PULSE_EXPORT
const char * pulse_media_direction_to_string (PulseMediaDirection direction);

/**
 * @brief pulse_media_content_to_string
 * Converts a PulseMediaContent to its string representation.
 * @param media_content The media content type to convert.
 * @return A string representation of the media content type.
 */
PULSE_EXPORT
const char * pulse_media_content_to_string (PulseMediaContent media_content);

PULSE_DECL_END

#endif /* _PULSE_MEDIA_H_ */