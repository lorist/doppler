/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2022 Pexip AS
 * @author Knut Saastad
 * @author Tulio Beloqui
 */

#ifndef _PULSE_PEXCISION_SESSION_H_
#define _PULSE_PEXCISION_SESSION_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "pulse_config.h"
#include "pulse_error.h"

PULSE_DECL_BEGIN

/** @brief Opaque configuration for a pexcision audio input session. */
typedef struct _PulsePexcisionAudioInputConfig PulsePexcisionAudioInputConfig;
/** @brief Opaque configuration for a pexcision audio output session. */
typedef struct _PulsePexcisionAudioOutputConfig PulsePexcisionAudioOutputConfig;
/** @brief Opaque configuration for a pexcision video input session. */
typedef struct _PulsePexcisionVideoInputConfig PulsePexcisionVideoInputConfig;
/** @brief Opaque configuration for a pexcision video output session. */
typedef struct _PulsePexcisionVideoOutputConfig PulsePexcisionVideoOutputConfig;

/** @brief Identifier for a pexcision input stream. */
typedef uint32_t PulseInputID;
/** @brief Identifier for a pexcision output stream. */
typedef uint32_t PulseOutputID;

/**
 * @brief Audio quality statistics.
 * Contains timing information for detected audio clicks.
 */
typedef struct _PulseAudioQualityStats
{
  unsigned int * clicks_times;
  int clicks_len;
} PulseAudioQualityStats;

/**
 * @brief Video stream information.
 * Describes the resolution and framerate of a video stream.
 */
typedef struct _PulseVideoInfo
{
  int width;
  int height;
  int fps_n;
  int fps_d;
} PulseVideoInfo;

/**
 * @brief Region of Interest descriptor.
 * Defines a rectangular region within a video frame.
 */
typedef struct _PulseRoI
{
  int id;
  int x;
  int y;
  int w;
  int h;
} PulseRoI;

/** @brief Callback invoked with a timestamp (e.g. beep sent/received). */
typedef void (*PulsePexcisionTimestampCB) (void * user_data, uint64_t timestamp);
/** @brief Callback invoked with audio quality statistics for an output. */
typedef void (*PulsePexcisionAudioQualityStatsCB) (void * user_data, PulseOutputID output_id,
                                                   const PulseAudioQualityStats * stats);
/** @brief Callback invoked with the participant list for an output. */
typedef void (*PulsePexcisionParticipantListCB) (void * user_data, PulseOutputID output_id, uint32_t * pexcision_ids,
                                                 int len);
/** @brief Callback invoked with video info updates for an output. */
typedef void (*PulsePexcisionVideoInfoCB) (void * user_data, PulseOutputID output_id, PulseVideoInfo * video_info);
/** @brief Callback invoked with a list of regions of interest for an output. */
typedef void (*PulsePexcisionRoIListCB) (void * user_data, PulseOutputID output_id, PulseRoI * rois, int rois_len);

/**
 * @brief pulse_pexcision_audio_input_config_new
 * Creates a new PulsePexcisionAudioInputConfig object.
 * @return A pointer to a new config instance, or NULL on failure.
 */
PULSE_EXPORT
PulsePexcisionAudioInputConfig * pulse_pexcision_audio_input_config_new (void);

/**
 * @brief pulse_pexcision_audio_input_config_free
 * Frees a PulsePexcisionAudioInputConfig object.
 * @param config The config to free.
 */
PULSE_EXPORT
void pulse_pexcision_audio_input_config_free (PulsePexcisionAudioInputConfig * config);

/**
 * @brief pulse_pexcision_audio_input_config_set_mode
 * @param config The audio input config.
 * @param mode The input mode to set.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_audio_input_config_set_mode (PulsePexcisionAudioInputConfig * config, int32_t mode);

/**
 * @brief pulse_pexcision_audio_input_config_set_filename
 * @param config The audio input config.
 * @param filename Path to the audio file.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_audio_input_config_set_filename (PulsePexcisionAudioInputConfig * config, const char * filename);

/**
 * @brief pulse_pexcision_audio_input_config_set_samplesperbuffer
 * @param config The audio input config.
 * @param samplesperbuffer Number of samples per buffer.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_audio_input_config_set_samplesperbuffer (PulsePexcisionAudioInputConfig * config,
                                                              int32_t samplesperbuffer);

/**
 * @brief pulse_pexcision_audio_input_config_set_beep_duration
 * @param config The audio input config.
 * @param beep_duration Beep duration in milliseconds.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_audio_input_config_set_beep_duration (PulsePexcisionAudioInputConfig * config,
                                                           int32_t beep_duration);

/**
 * @brief pulse_pexcision_audio_input_config_set_beep_period
 * @param config The audio input config.
 * @param beep_period Beep period in milliseconds.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_audio_input_config_set_beep_period (PulsePexcisionAudioInputConfig * config, int32_t beep_period);

/**
 * @brief pulse_pexcision_audio_input_config_set_beep_delay
 * @param config The audio input config.
 * @param beep_delay Beep delay in milliseconds.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_audio_input_config_set_beep_delay (PulsePexcisionAudioInputConfig * config, int32_t beep_delay);

/**
 * @brief pulse_pexcision_audio_input_config_set_latency
 * @param config The audio input config.
 * @param latency Latency in milliseconds.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_audio_input_config_set_latency (PulsePexcisionAudioInputConfig * config, int32_t latency);

/**
 * @brief pulse_pexcision_audio_input_config_set_volume
 * @param config The audio input config.
 * @param volume Volume level (0.0 to 1.0).
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_audio_input_config_set_volume (PulsePexcisionAudioInputConfig * config, double volume);

/**
 * @brief pulse_pexcision_audio_input_config_set_loop
 * @param config The audio input config.
 * @param loop Whether to loop the audio input.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_audio_input_config_set_loop (PulsePexcisionAudioInputConfig * config, bool loop);

/**
 * @brief pulse_pexcision_audio_input_config_set_freq
 * @param config The audio input config.
 * @param freq Frequency in Hz.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_audio_input_config_set_freq (PulsePexcisionAudioInputConfig * config, uint32_t freq);

/**
 * @brief pulse_pexcision_audio_input_config_set_num_buffers
 * @param config The audio input config.
 * @param num_buffers Number of buffers to produce before stopping.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_audio_input_config_set_num_buffers (PulsePexcisionAudioInputConfig * config, uint32_t num_buffers);

/**
 * @brief pulse_pexcision_audio_input_config_set_beep_sent_cb
 * Sets the callback invoked when a beep is sent.
 * @param config The audio input config.
 * @param beep_sent_cb The callback function.
 * @param user_data User context passed to the callback.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_audio_input_config_set_beep_sent_cb (PulsePexcisionAudioInputConfig * config,
                                                          PulsePexcisionTimestampCB beep_sent_cb, void * user_data);

/**
 * @brief pulse_pexcision_audio_output_config_new
 * Creates a new PulsePexcisionAudioOutputConfig object.
 * @return A pointer to a new config instance, or NULL on failure.
 */
PULSE_EXPORT
PulsePexcisionAudioOutputConfig * pulse_pexcision_audio_output_config_new (void);

/**
 * @brief pulse_pexcision_audio_output_config_free
 * Frees a PulsePexcisionAudioOutputConfig object.
 * @param config The config to free.
 */
PULSE_EXPORT
void pulse_pexcision_audio_output_config_free (PulsePexcisionAudioOutputConfig * config);

/**
 * @brief pulse_pexcision_audio_output_config_set_fft_mag_threshold
 * @param config The audio output config.
 * @param fft_mag_threshold FFT magnitude threshold.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_audio_output_config_set_fft_mag_threshold (PulsePexcisionAudioOutputConfig * config,
                                                                double fft_mag_threshold);

/**
 * @brief pulse_pexcision_audio_output_config_set_fft_mag_resolution
 * @param config The audio output config.
 * @param fft_resolution FFT magnitude resolution.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_audio_output_config_set_fft_mag_resolution (PulsePexcisionAudioOutputConfig * config,
                                                                 int fft_resolution);

/**
 * @brief pulse_pexcision_audio_output_config_set_silence_threshold_db
 * @param config The audio output config.
 * @param silence_threshold_db Silence threshold in dB.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_audio_output_config_set_silence_threshold_db (PulsePexcisionAudioOutputConfig * config,
                                                                   double silence_threshold_db);

/**
 * @brief pulse_pexcision_audio_output_config_set_fft_required_samples
 * @param config The audio output config.
 * @param fft_required_samples Number of samples required for FFT analysis.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_audio_output_config_set_fft_required_samples (PulsePexcisionAudioOutputConfig * config,
                                                                   int fft_required_samples);

/**
 * @brief pulse_pexcision_audio_output_config_set_filename
 * @param config The audio output config.
 * @param filename Path to the output audio file.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_audio_output_config_set_filename (PulsePexcisionAudioOutputConfig * config, const char * filename);

/**
 * @brief pulse_pexcision_audio_output_config_set_audioq_stats_cb
 * Sets the callback invoked with audio quality statistics.
 * @param config The audio output config.
 * @param audioq_stats_cb The callback function.
 * @param user_data User context passed to the callback.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_audio_output_config_set_audioq_stats_cb (PulsePexcisionAudioOutputConfig * config,
                                                              PulsePexcisionAudioQualityStatsCB audioq_stats_cb,
                                                              void * user_data);

/**
 * @brief pulse_pexcision_audio_output_config_set_plist_cb
 * Sets the callback invoked with the participant list.
 * @param config The audio output config.
 * @param plist_cb The callback function.
 * @param user_data User context passed to the callback.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_audio_output_config_set_plist_cb (PulsePexcisionAudioOutputConfig * config,
                                                       PulsePexcisionParticipantListCB plist_cb, void * user_data);

/**
 * @brief pulse_pexcision_audio_output_config_set_beep_recv_cb
 * Sets the callback invoked when a beep is received.
 * @param config The audio output config.
 * @param beep_recv_cb The callback function.
 * @param user_data User context passed to the callback.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_audio_output_config_set_beep_recv_cb (PulsePexcisionAudioOutputConfig * config,
                                                           PulsePexcisionTimestampCB beep_recv_cb, void * user_data);

/**
 * @brief pulse_pexcision_video_input_config_new
 * Creates a new PulsePexcisionVideoInputConfig object.
 * @return A pointer to a new config instance, or NULL on failure.
 */
PULSE_EXPORT
PulsePexcisionVideoInputConfig * pulse_pexcision_video_input_config_new (void);

/**
 * @brief pulse_pexcision_video_input_config_free
 * Frees a PulsePexcisionVideoInputConfig object.
 * @param config The config to free.
 */
PULSE_EXPORT
void pulse_pexcision_video_input_config_free (PulsePexcisionVideoInputConfig * config);

/**
 * @brief pulse_pexcision_video_input_config_set_video_info
 * Sets the video resolution and framerate for the input.
 * @param config The video input config.
 * @param width Frame width in pixels.
 * @param height Frame height in pixels.
 * @param fps_n Framerate numerator.
 * @param fps_d Framerate denominator.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_video_input_config_set_video_info (PulsePexcisionVideoInputConfig * config, int width, int height,
                                                        int fps_n, int fps_d);

/**
 * @brief pulse_pexcision_video_input_config_add_roi
 * Adds a region of interest to the video input configuration.
 * @param config The video input config.
 * @param id Region of interest identifier.
 * @param x X coordinate of the region.
 * @param y Y coordinate of the region.
 * @param w Width of the region.
 * @param h Height of the region.
 */
PULSE_EXPORT
void pulse_pexcision_video_input_config_add_roi (PulsePexcisionVideoInputConfig * config, int id, int x, int y, int w,
                                                 int h);

/**
 * @brief pulse_pexcision_video_input_config_set_flash_period_ms
 * @param config The video input config.
 * @param flash_period_ms Pointer to the flash period in milliseconds.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_video_input_config_set_flash_period_ms (PulsePexcisionVideoInputConfig * config,
                                                             unsigned int * flash_period_ms);

/**
 * @brief pulse_pexcision_video_input_config_set_flash_duration_ms
 * @param config The video input config.
 * @param flash_duration_ms Pointer to the flash duration in milliseconds.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_video_input_config_set_flash_duration_ms (PulsePexcisionVideoInputConfig * config,
                                                               unsigned int * flash_duration_ms);

/**
 * @brief pulse_pexcision_video_input_config_set_sync_test
 * @param config The video input config.
 * @param sync_test Whether to enable sync testing.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_video_input_config_set_sync_test (PulsePexcisionVideoInputConfig * config, bool sync_test);

/**
 * @brief pulse_pexcision_video_input_config_set_id
 * @param config The video input config.
 * @param id The input identifier.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_video_input_config_set_id (PulsePexcisionVideoInputConfig * config, uint32_t id);

/**
 * @brief pulse_pexcision_video_input_config_set_num_buffers
 * @param config The video input config.
 * @param num_buffers Number of buffers to produce before stopping.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_video_input_config_set_num_buffers (PulsePexcisionVideoInputConfig * config, uint32_t num_buffers);

/**
 * @brief pulse_pexcision_video_input_config_set_beep_recv_cb
 * Sets the timestamp callback for the video input.
 * @param config The video input config.
 * @param flash_sent_cb The callback function.
 * @param user_data User context passed to the callback.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_video_input_config_set_beep_recv_cb (PulsePexcisionVideoInputConfig * config,
                                                          PulsePexcisionTimestampCB flash_sent_cb, void * user_data);

/**
 * @brief pulse_pexcision_video_output_config_new
 * Creates a new PulsePexcisionVideoOutputConfig object.
 * @return A pointer to a new config instance, or NULL on failure.
 */
PULSE_EXPORT
PulsePexcisionVideoOutputConfig * pulse_pexcision_video_output_config_new (void);

/**
 * @brief pulse_pexcision_video_output_config_free
 * Frees a PulsePexcisionVideoOutputConfig object.
 * @param config The config to free.
 */
PULSE_EXPORT
void pulse_pexcision_video_output_config_free (PulsePexcisionVideoOutputConfig * config);

/**
 * @brief pulse_pexcision_video_output_config_set_num_participants
 * @param config The video output config.
 * @param num_participants Number of participants in the output.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_video_output_config_set_num_participants (PulsePexcisionVideoOutputConfig * config,
                                                               int num_participants);

/**
 * @brief pulse_pexcision_video_output_config_set_plist_cb
 * Sets the callback invoked with the participant list.
 * @param config The video output config.
 * @param plist_cb The callback function.
 * @param user_data User context passed to the callback.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_video_output_config_set_plist_cb (PulsePexcisionVideoOutputConfig * config,
                                                       PulsePexcisionParticipantListCB plist_cb, void * user_data);

/**
 * @brief pulse_pexcision_video_output_config_set_vinfo_cb
 * Sets the callback invoked with video info updates.
 * @param config The video output config.
 * @param vinfo_cb The callback function.
 * @param user_data User context passed to the callback.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_video_output_config_set_vinfo_cb (PulsePexcisionVideoOutputConfig * config,
                                                       PulsePexcisionVideoInfoCB vinfo_cb, void * user_data);

/**
 * @brief pulse_pexcision_video_output_config_set_roi_list_cb
 * Sets the callback invoked with the region of interest list.
 * @param config The video output config.
 * @param roi_list_cb The callback function.
 * @param user_data User context passed to the callback.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_video_output_config_set_roi_list_cb (PulsePexcisionVideoOutputConfig * config,
                                                          PulsePexcisionRoIListCB roi_list_cb, void * user_data);

/**
 * @brief pulse_pexcision_video_output_config_set_dump
 * @param config The video output config.
 * @param dump Path for dumping video output data.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_video_output_config_set_dump (PulsePexcisionVideoOutputConfig * config, const char * dump);

/**
 * @brief pulse_pexcision_video_output_config_set_sync_test
 * @param config The video output config.
 * @param sync_test Whether to enable sync testing.
 * @return true on success, false on failure.
 */
PULSE_EXPORT
bool pulse_pexcision_video_output_config_set_sync_test (PulsePexcisionVideoOutputConfig * config, bool sync_test);

/**
 * @brief pulse_pexcision_session_connect_audio
 * Connects a pexcision audio session for the specified media content and participant.
 * @param client The Pulse handle.
 * @param media_content The media content slot to use.
 * @param participant_id The participant identifier.
 * @param config_input Audio input configuration, or NULL for output-only.
 * @param config_output Audio output configuration, or NULL for input-only.
 * @return PULSE_SUCCESS on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_pexcision_session_connect_audio (Pulse * client, PulseMediaContent media_content,
                                                  uint32_t participant_id,
                                                  PulsePexcisionAudioInputConfig * config_input,
                                                  PulsePexcisionAudioOutputConfig * config_output);

/**
 * @brief pulse_pexcision_session_connect_video
 * Connects a pexcision video session for the specified media content and participant.
 * @param client The Pulse handle.
 * @param media_content The media content slot to use.
 * @param participant_id The participant identifier.
 * @param config_input Video input configuration, or NULL for output-only.
 * @param config_output Video output configuration, or NULL for input-only.
 * @return PULSE_SUCCESS on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_pexcision_session_connect_video (Pulse * client, PulseMediaContent media_content,
                                                  uint32_t participant_id,
                                                  PulsePexcisionVideoInputConfig * config_input,
                                                  PulsePexcisionVideoOutputConfig * config_output);

/**
 * @brief pulse_pexcision_session_disconnect
 * Disconnects a pexcision session for the specified media content, type, and direction.
 * @param client The Pulse handle.
 * @param media_content The media content slot.
 * @param media_type The media type (audio or video).
 * @param media_direction The media direction (input or output).
 * @return PULSE_SUCCESS on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_pexcision_session_disconnect (Pulse * client, PulseMediaContent media_content,
                                               PulseMediaType media_type, PulseMediaDirection media_direction);

/**
 * @brief pulse_pexcision_session_start
 * Starts all pexcision sessions for the specified media content.
 * @param client The Pulse handle.
 * @param media_content The media content slot.
 * @return PULSE_SUCCESS on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_pexcision_session_start (Pulse * client, PulseMediaContent media_content);

/**
 * @brief pulse_pexcision_session_start_audio
 * Starts the pexcision audio session for the specified media content.
 * @param client The Pulse handle.
 * @param media_content The media content slot.
 * @return PULSE_SUCCESS on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_pexcision_session_start_audio (Pulse * client, PulseMediaContent media_content);

/**
 * @brief pulse_pexcision_session_start_video
 * Starts the pexcision video session for the specified media content.
 * @param client The Pulse handle.
 * @param media_content The media content slot.
 * @return PULSE_SUCCESS on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_pexcision_session_start_video (Pulse * client, PulseMediaContent media_content);

/**
 * @brief pulse_pexcision_session_stop
 * Stops all pexcision sessions for the specified media content.
 * @param client The Pulse handle.
 * @param media_content The media content slot.
 * @return PULSE_SUCCESS on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_pexcision_session_stop (Pulse * client, PulseMediaContent media_content);

/**
 * @brief pulse_pexcision_session_stop_audio
 * Stops the pexcision audio session for the specified media content.
 * @param client The Pulse handle.
 * @param media_content The media content slot.
 * @return PULSE_SUCCESS on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_pexcision_session_stop_audio (Pulse * client, PulseMediaContent media_content);

/**
 * @brief pulse_pexcision_session_stop_video
 * Stops the pexcision video session for the specified media content.
 * @param client The Pulse handle.
 * @param media_content The media content slot.
 * @return PULSE_SUCCESS on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_pexcision_session_stop_video (Pulse * client, PulseMediaContent media_content);

PULSE_DECL_END

#endif
