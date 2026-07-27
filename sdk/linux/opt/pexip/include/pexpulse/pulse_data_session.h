/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2022 Pexip AS
 * @author Knut Saastad
 * @author Tulio Beloqui
 */

#ifndef _PULSE_DATA_SESSION_H_
#define _PULSE_DATA_SESSION_H_

#include "pulse_error.h"
#include "pulse_media.h"

// Enumeration types
typedef enum _PulseDataSessionTypeMask
{
  PULSE_DATA_SESSION_AUDIO_FROM_CAPS = 1 << 0,
  PULSE_DATA_SESSION_AUDIO_FROM_VALUES = 1 << 1,
  PULSE_DATA_SESSION_VIDEO_FROM_CAPS = 1 << 2,
  PULSE_DATA_SESSION_VIDEO_FROM_VALUES = 1 << 3,
  PULSE_DATA_SESSION_AUDIO_FROM_CAPS_VIDEO_FROM_CAPS =
    (PULSE_DATA_SESSION_AUDIO_FROM_CAPS | PULSE_DATA_SESSION_VIDEO_FROM_CAPS),
  PULSE_DATA_SESSION_AUDIO_FROM_CAPS_VIDEO_FROM_VALUES =
    (PULSE_DATA_SESSION_AUDIO_FROM_CAPS | PULSE_DATA_SESSION_VIDEO_FROM_VALUES),
  PULSE_DATA_SESSION_AUDIO_FROM_VALUES_VIDEO_FROM_CAPS =
    (PULSE_DATA_SESSION_AUDIO_FROM_VALUES | PULSE_DATA_SESSION_VIDEO_FROM_CAPS),
  PULSE_DATA_SESSION_AUDIO_FROM_VALUES_VIDEO_FROM_VALUES =
    (PULSE_DATA_SESSION_AUDIO_FROM_VALUES | PULSE_DATA_SESSION_VIDEO_FROM_VALUES),
} PulseDataSessionTypeMask;

// Opaque types
typedef struct _PulseDataSessionConfig PulseDataSessionConfig;

// Transparent types
typedef struct _PulseDimensions
{
  uint32_t width;
  uint32_t height;
} PulseDimensions;

typedef struct _PulseFramerate
{
  uint32_t numerator;
  uint32_t denominator;
} PulseFramerate;

/**
 * @brief PulseDataSessionFrameData
 * The PulseDataSessionFrameData structure allows you to specify a data pointer, specify the size of the data.
 */
typedef struct _PulseDataSessionFrameData
{
  const uint8_t * data;
  int data_size;
} PulseDataSessionFrameData;

/**
 * @brief PulseDataSessionFrame
 * The PulseDataSessionFrame is used for pushing raw data frames to Pulse.
 * Update_config allows you to update a running data_session with new config. This config needs only be specified when a
 * change is done. The config must be complete, any attempt at doing a partial update will probably fail. At least one
 * of audio or video framedata should be specified, and both can be specified as one time.
 */
typedef struct _PulseDataSessionFrame
{
  PulseDataSessionConfig * update_config;
  PulseDataSessionFrameData audio;
  PulseDataSessionFrameData video;
} PulseDataSessionFrame;

PULSE_DECL_BEGIN

/**
 * @brief pulse_data_session_config_new
 * Creates a new PulseDataSessionConfig object of the specified type.
 * @param session_type_mask Type of PulseDataSessionConfig to create. Can be any combination of the values defined in
 * PulseDataSessionTypeMask.
 * @return A pointer to a new PulseDataSessionConfig instance, or NULL if the creation failed.
 */
PULSE_EXPORT
PulseDataSessionConfig * pulse_data_session_config_new (PulseDataSessionTypeMask session_type_mask);

/**
 * @brief pulse_data_session_config_free
 * Free up any memory allocated for the PulseDataSessionConfig instance
 * @param config A reference to a PulseDataSessionConfig instance.
 */
PULSE_EXPORT
void pulse_data_session_config_free (PulseDataSessionConfig * config);

/**
 * @brief pulse_data_session_config_audio_from_caps
 * Sets the full audio CAPS string for a PulseDataSessionConfig created with PULSE_DATA_SESSION_AUDIO_FROM_CAPS as part
 * of its session_type_mask.
 * @param config pointer to a PulseDataSessionConfig instance.
 * @param caps A string containing the full audiocaps. The CAPS type is expected to be with 'audio/x-raw'.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_data_session_config_audio_from_caps (PulseDataSessionConfig * config, const char * caps);

/**
 * @brief pulse_data_session_config_audio_from_values
 * Sets the configuration values for the audio session.
 * @param config pointer to a PulseDataSessionConfig instance.
 * @param audio_format The audioformat to configure the data-session as.
 * @param audio_layout The layout of the samples.
 * @param rate The configured audio data rate (bps).
 * @param channels The number of channels the audio channels the audio input consists of.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_data_session_config_audio_from_values (PulseDataSessionConfig * config,
                                                        PulseMediaAudioFormat audio_format,
                                                        PulseMediaAudioLayout audio_layout, uint32_t rate,
                                                        uint32_t channels);

/**
 * @brief pulse_data_session_config_audio_from_values_set_additional_caps
 * Sets any additional caps, that is not already configured by the value parameters.
 * @param config pointer to a PulseDataSessionConfig instance.
 * @param additional_caps A string containing the additional audio caps. The caps may only specify properties, and
 * cannot include any of the following properties: media, format, rate, channels.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_data_session_config_audio_from_values_set_additional_caps (PulseDataSessionConfig * config,
                                                                            const char * additional_caps);

/**
 * @brief pulse_data_session_config_video_from_caps
 * Sets the full audio CAPS string for a PulseDataSessionConfig created with PULSE_DATA_SESSION_VIDEO_FROM_CAPS as part
 * of its session_type_mask.
 * @param config pointer to a PulseDataSessionConfig instance.
 * @param caps A string containing the full video caps. The CAPS type is expected to be with 'video/x-raw'.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_data_session_config_video_from_caps (PulseDataSessionConfig * config, const char * caps);

/**
 * @brief pulse_data_session_config_video_from_values
 * Sets the configuration values for the video session.
 * @param config pointer to a PulseDataSessionConfig instance.
 * @param pixel_format The pixel format that the input video data will be pushed as.
 * @param dimensions The width and height dimensions of the input data.
 * @param framerate The framerate of the input data.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_data_session_config_video_from_values (PulseDataSessionConfig * config,
                                                        PulseMediaPixelFormat pixel_format, PulseDimensions dimensions,
                                                        PulseFramerate framerate);

/**
 * @brief pulse_data_session_config_video_from_values_set_additional_caps
 * Sets any additional caps, that is not already configured by the value parameters.
 * @param config pointer to a PulseDataSessionConfig instance.
 * @param additional_caps A string containing the additional video caps. The caps may only specify properties, and
 * cannot include any of the following properties: media, format, width, height, framerate.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_data_session_config_video_from_values_set_additional_caps (PulseDataSessionConfig * config,
                                                                            const char * additional_caps);

/**
 * @brief pulse_data_session_connect_input
 * Start a new data session using the provided configuration.
 * @param client The Pulse handle
 * @param config pointer to a PulseDataSessionConfig instance.
 * @param media_content Indicates which media content (MAIN or PRESENTATION), the data session will be linked to.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_data_session_connect_input (Pulse * client, const PulseDataSessionConfig * config,
                                             PulseMediaContent media_content);

/**
 * @brief pulse_data_session_connect_output
 * Start a new data session using the provided configuration.
 * @param client The Pulse handle
 * @param config pointer to a PulseDataSessionConfig instance.
 * @param media_content Indicates which media content (MAIN or PRESENTATION), the data session will be linked to.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_data_session_connect_output (Pulse * client, const PulseDataSessionConfig * config,
                                              PulseMediaContent media_content);

/**
 * @brief pulse_data_session_disconnect
 * Disconnects a previous connected INPUT or OUTPUT
 * @param client The Pulse handle
 * @param media_type The PulseMediaType to disconnect
 * @param media_direction Indicates if you want to disconnect an INPUT or an OUTPUT
 * @param media_content Indicates which media content (MAIN or PRESENTATION), the data session will be linked to.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_data_session_disconnect (Pulse * client, PulseMediaType media_type,
                                          PulseMediaDirection media_direction, PulseMediaContent media_content);

/**
 * @brief pulse_data_session_push_frame
 * Push a new frame of data (audio, video or both), to a previously connected data_session.
 * @param client The Pulse handle
 * @param frame The audio and/or video data to send.
 * @param media_content Indicates which media content (MAIN or PRESENTATION), the data session will be linked to.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_data_session_push_frame (Pulse * client, const PulseDataSessionFrame * frame,
                                          PulseMediaContent media_content);

/**
 * @brief pulse_data_session_pull_frame_data
 * Pull a new frame of data (audio or video or both), to a previously connected data_session.
 * @param client The Pulse handle
 * @param media_type The PulseMediaType to pull
 * @param frame_data The audio or video data to receive.
 * @param media_content Indicates which media content (MAIN or PRESENTATION), the data session will be linked to.
 * @param timeout Indicates the amount of time to wait for data to become available.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_data_session_pull_frame_data (Pulse * client, PulseMediaType media_type,
                                               PulseDataSessionFrameData ** frame_data, PulseMediaContent media_content,
                                               PulseTimestamp timeout);

/**
 * @brief pulse_frame_data_get_resolution
 * Get the width and height of a frame.
 * @param frame_data A frame retrieved via a previous call to pulse_data_session_pull_frame_data()
 * @param width Pointer to the int where the frame's width will be written.
 * @param height Pointer to the int where the frame's width will be written.
 * @return True if the frame's width and height could be successfully retrieved, otherwise false.
 */
PULSE_EXPORT
bool pulse_frame_data_get_resolution (PulseDataSessionFrameData * frame_data, int * width, int * height);

/**
 * @brief pulse_data_session_frame_data_free
 * Free all memory associated with data frame.
 * @param frame_data A frame retrieved via a previous call to pulse_data_session_pull_frame_data()
 */
PULSE_EXPORT
void pulse_data_session_frame_data_free (PulseDataSessionFrameData * frame_data);

/**
 * @brief pulse_data_session_get_input_config
 * Get the input config for data_session.
 * @param client The Pulse handle
 * @param media_content Which media content (MAIN or PRESENTATION) to request the input config for.
 * @param config a pointer to the PulseDataSessionConfig * where the copy of the config will be referenced.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note The returned config is owned by the caller, and must be freed by calling pulse_data_session_config_free() after
 * use.
 */
PULSE_EXPORT
PulseError pulse_data_session_get_input_config (Pulse * client, PulseMediaContent media_content,
                                                PulseDataSessionConfig ** config);

/**
 * @brief pulse_data_session_get_output_config
 * Get the output config for data_session.
 * @param client The Pulse handle
 * @param media_content Which media content (MAIN or PRESENTATION) to request the outout config for.
 * @param config a pointer to the PulseDataSessionConfig * where the copy of the config will be referenced.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note The returned config is owned by the caller, and must be freed by calling pulse_data_session_config_free() after
 * use.
 */
PULSE_EXPORT
PulseError pulse_data_session_get_output_config (Pulse * client, PulseMediaContent media_content,
                                                 PulseDataSessionConfig ** config);

/**
 * @brief pulse_data_session_config_get_audio_caps
 * Get the configured audio caps of PulseDataSessionConfig previously configure as AUDIO_FROM_CAPS.
 * @param config pointer to a PulseDataSessionConfig instance.
 * @param caps a pointer to a const char * where the configured caps string will be referenced. The returned string is
 * owned by the config, and must not be freed directly.
 */
PULSE_EXPORT
PulseError pulse_data_session_config_get_audio_caps (PulseDataSessionConfig * config, const char ** caps);

/**
 * @brief pulse_data_session_config_get_audio_values
 * Get the configured audio values of PulseDataSessionConfig previously configure as AUDIO_FROM_VALUES.
 * @param config pointer to a PulseDataSessionConfig instance.
 * @param audio_format a pointer to where the configured audio format will be stored.
 * @param audio_layout a pointer to where the configured audio layout will be stored.
 * @param rate a pointer to where the configured rate will be stored.
 * @param channels a pointer to where the configured channels will be stored.
 * @param additional_caps a pointer to a const char * where the configured additional caps string will be referenced.
 * The returned string is owned by the config, and must not be freed directly.
 */
PULSE_EXPORT
PulseError pulse_data_session_config_get_audio_values (PulseDataSessionConfig * config,
                                                       PulseMediaAudioFormat * audio_format,
                                                       PulseMediaAudioLayout * audio_layout, uint32_t * rate,
                                                       uint32_t * channels, const char ** additional_caps);

/**
 * @brief pulse_data_session_config_get_video_caps
 * Get the configured video caps of PulseDataSessionConfig previously configure as VIDEO_FROM_CAPS.
 * @param config pointer to a PulseDataSessionConfig instance.
 * @param caps a pointer to a const char * where the configured caps string will be referenced. The returned string is
 * owned by the config, and must not be freed directly.
 */
PULSE_EXPORT
PulseError pulse_data_session_config_get_video_caps (PulseDataSessionConfig * config, const char ** caps);

/**
 * @brief pulse_data_session_config_get_video_values
 * Get the configured video values of PulseDataSessionConfig previously configure as VIDEO_FROM_VALUES.
 * @param config pointer to a PulseDataSessionConfig instance.
 * @param pixel_format a pointer to where the configured pixel_format will be stored.
 * @param dimensions a pointer to where the configured dimensions will be stored.
 * @param framerate a pointer to where the configured framerate will be stored.
 * @param additional_caps a pointer to a const char * where the configured additional caps string will be referenced.
 * The returned string is owned by the config, and must not be freed directly.
 */
PULSE_EXPORT
PulseError pulse_data_session_config_get_video_values (PulseDataSessionConfig * config,
                                                       PulseMediaPixelFormat * pixel_format,
                                                       PulseDimensions * dimensions, PulseFramerate * framerate,
                                                       const char ** additional_caps);

PULSE_DECL_END

#endif /* _PULSE_DATA_SESSION_H_ */