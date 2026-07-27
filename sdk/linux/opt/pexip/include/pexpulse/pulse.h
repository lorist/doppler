/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2022 Pexip AS
 * @author Knut Saastad
 * @author Tulio Beloqui
 */

#ifndef _PULSE_H_
#define _PULSE_H_

#include <stddef.h>

#include "pulse_types.h"
#include "pulse_type_mappings.h"
#include "pulse_config.h"
#include "pulse_media.h"
#include "pulse_media_stats.h"
#include "pulse_error.h"
#include "pulse_fecc_types.h"
#include "pulse_ptz_types.h"
#include "pulse_options.h"
#include "pulse_data_session.h"
#include "pulse_device.h"
#include "pulse_device_session.h"
#include "pulse_file_session.h"
#include "pulse_conference.h"
#include "pulse_conference_control.h"
#include "pulse_participant_control.h"
#include "pulse_participant_list.h"
#include "pulse_debug.h"
#include "pulse_registrations.h"
#include "pulse_rtmp_session.h"
#include "pulse_rtp_session.h"
#include "pulse_rtsp_session.h"
#include "pulse_breakout_rooms.h"
#include "pulse_ipc.h"
#include "pulse_video_mix_session.h"
#include "pulse_video_mix_input.h"
#include "pulse_annotation.h"
PULSE_DECL_BEGIN

/**
 * @brief Set a callback function to which all log output will be sent to.
 * This registers a callback with the pulse logging system.
 * Note: This must be registered prior to instanciating a Pulse handle.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_global_logger_callback (PulseLoggingCallback func, void * user_context);

/**
 * @brief Instantiate a new Pulse.
 * Instantiates and initializes a new Pulse for use with internal rest handling.
 * @return Pulse handle, or NULL on error.
 */
PULSE_EXPORT
Pulse * pulse_new (void);

/**
 * @brief Instantiate a new Pulse with external rest handling.
 * Instantiates and initializes a new Pulse for use with external rest handling.
 * @return Pulse handle, or NULL on error.
 */
PULSE_EXPORT
Pulse * pulse_new_external_rest (PulseExternalRestCallbackConfig external_rest_config);

// On Windows: we only support to use pulse_new() and pulse_options_set_sso_provider_callbacks()
#if !defined(HOST_WINDOWS)

/**
 * @brief Instantiate a new Pulse with internal sso/idp handling.
 * Instantiates and initializes a new Pulse for use with internal rest handling, that handles sso/idp internally.
 * NOTE: It is the responsibility of the implementation to set up deep-link handling of pexip-auth:// URLs, which is
 * needed to make SSO work.
 * @return Pulse handle, or NULL on error.
 */
PULSE_EXPORT
Pulse * pulse_new_with_internal_sso_handling (int argc, const char ** argv,
                                              PulseSSOProviderSelectionCallback selection_callback,
                                              void * selection_callback_user_context);

#endif /* !defined(HOST_WINDOWS) */

/**
 * @brief Frees a previously instantiated Pulse.
 * Frees a previously instantiated Pulse.
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_free (Pulse * client);

/**
 * @brief Pulse with internal REST communication handling.
 * This function handles all parts of the communication setup - provide it with a set of connections parameters, and it
 * takes will setup all you need. In many cases, you want to set one or more pulse_options prior to calling connect, to
 * set specific behavior or registering callbacks for eg SSO.
 * @param client The Pulse handle
 * @param connection_params PulseRestConnectionConfig struct type, with the necessary information filled in.
 * @param progress_callback_config An optional callback function with user_context, for receiving progress updates
 * during disconnect.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available when running the Pulse in internal rest handling modes
 */
PULSE_EXPORT
PulseError pulse_connect_with_rest (Pulse * client, PulseRestConnectionConfig * connection_params,
                                    PulseOperationProgressCallbackConfig * progress_callback_config);

/**
 * @brief Pulse with internal REST communication handling.
 * This function handles all parts of the communication setup - provide it with a set of connections parameters, and it
 * takes will setup all you need. In many cases, you want to set one or more pulse_options prior to calling connect, to
 * set specific behavior or registering callbacks for eg SSO.
 * @param client The Pulse handle
 * @param connection_params PulseRestConnectionConfig struct type, with the necessary information filled in.
 * @param result_callback_config Mandatory callback function for receiving the error code from the execution of the
 * async function, once it is done.
 * @param progress_callback_config An optional callback function with user_context, for receiving progress updates
 * during disconnect.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available when running the Pulse in internal rest handling modes
 */
PULSE_EXPORT
PulseError pulse_connect_with_rest_async (Pulse * client, PulseRestConnectionConfig * connection_params,
                                          PulseAsyncOperationResultCallbackConfig * result_callback_config,
                                          PulseOperationProgressCallbackConfig * progress_callback_config);

/**
 * @brief Pulse setup where caller controls REST server communication - buffer based - First stage out of two.
 * Pulse setup where user controls the communication with the REST server. Once the REST communication query to the
 * 'request_token' endpoint has succeeded, and a token has been retrieved, the full returned payload from the REST
 * server should be provided as input to this function.
 * @param client The Pulse handle
 * @param buf The full, response buffer from the REST server, after a complete session setup.
 * @param buf_size Size in bytes of the response buffer.
 * @param local_sdp Reference to a char pointer, where the local side sdp buffer will be returned.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_setup_stage_1_from_response_buffer (Pulse * client, const char * buf, const int buf_size,
                                                     const char ** local_sdp);

/**
 * @brief Pulse setup where caller controls REST server communication - buffer based - Second stage out of two.
 * Pulse setup where user controls the communication with the REST server. Once the REST communication query to the
 * `participants/<name>/calls` endpoint has succeeded, the full returned payload from the REST server should be provided
 * as input to this function. This will complete the session setup.
 * @param client The Pulse handle
 * @param buf The full, response buffer from the REST server, after a complete session setup.
 * @param buf_size Size in bytes of the response buffer.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_setup_stage_2_from_response_buffer (Pulse * client, const char * buf, const int buf_size);

/**
 * @brief Pulse setup where caller controls REST server communication - structured - First stage out of two.
 * Pulse setup where user controls the communication with the REST server. Once the REST communication query to the
 * 'request_token' endpoint has succeeded, and a token has been retrieved, the PulseSetupStage1Config should be filled
 * in with the corresponding values.
 * @param client The Pulse handle
 * @param config A pointer to a PulseSetupStage1Config, with the all needed values filled in.
 * @param local_sdp Reference to a char pointer, where the local side sdp buffer will be returned.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_setup_stage_1_from_structure (Pulse * client, PulseSetupStage1Config * config,
                                               const char ** local_sdp);

/**
 * @brief Pulse setup where caller controls REST server communication - structured - Second stage out of two.
 * Pulse setup where user controls the communication with the REST server. Once the REST communication query to the
 * `participants/<name>/calls` endpoint has succeeded, the full returned payload from the REST server should be provided
 * as input to this function. This will complete the session setup.
 * @param client The Pulse handle
 * @param config A pointer to a PulseSetupStage2Config, with the all needed values filled in.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_setup_stage_2_from_structure (Pulse * client, PulseSetupStage2Config * config);

/**
 * @brief Pulse disconnect/cleanup of any of the pulse_connect/pulse_setup_* function above.
 * This function disconnects any running sessions, and frees all data.
 * @param client The Pulse handle
 * @param progress_callback_config An optional callback function with user_context, for receiving progress updates
 * during disconnect.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_disconnect (Pulse * client, PulseOperationProgressCallbackConfig * progress_callback_config);

/**
 * @brief Async pulse disconnect/cleanup of any of the pulse_connect/pulse_setup_* function above.
 * This function disconnects any running sessions, and frees all data.
 * @param client The Pulse handle
 * @param result_callback_config Mandatory callback function for receiving the error code from the execution of the
 * async function, once it is done.
 * @param progress_callback_config An optional callback function with user_context, for receiving progress updates
 * during disconnect.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_disconnect_async (Pulse * client, PulseAsyncOperationResultCallbackConfig * result_callback_config,
                                   PulseOperationProgressCallbackConfig * progress_callback_config);

/**
 * @brief Cancel a request.
 * This function allows you to cancel a running pulse_connect_with_rest()/pulse_connect_with_rest_async() call.
 * If the process was succssfullt cancelled, the return code for that call will return PULSE_ERROR_PROCESS_ABORTED.
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available when running the Pulse in internal rest handling modes
 */
PULSE_EXPORT
PulseError pulse_cancel_request (Pulse * client);

PULSE_EXPORT
void pulse_get_media_debug_data (Pulse * client, PulseMediaDebugData * data);

PULSE_EXPORT
void pulse_get_info (Pulse * client, PulseInfo * data);

PULSE_EXPORT
void pulse_set_max_bitrate (Pulse * client, uint32_t max_bps);

PULSE_EXPORT
uint32_t pulse_get_max_bitrate (Pulse * client);

PULSE_EXPORT
bool pulse_is_connected (Pulse * client);

PULSE_EXPORT
void pulse_start_streaming_mode (Pulse * client);

PULSE_EXPORT
void pulse_start_audio_streaming_mode (Pulse * client);

PULSE_EXPORT
void pulse_stop_presenting (Pulse * client);

/**
 * @brief pulse_device_set_audio_output_volume
 * Set the volume of for the output device.
 * @param client The Pulse handle
 * @param volume The new volume level. Float in the range [0-1]
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_device_set_audio_output_volume (Pulse * client, float volume);

/**
 * @brief pulse_mute_audio_input
 * Mute the local audio source (eg. a microphone, a data-input or the audiosteam from a mp4 file)
 * @param client The Pulse handle
 * @param mute TRUE to mute, FALSE to unmute.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_mute_audio_input (Pulse * client, bool mute);

/**
 * @brief pulse_mute_video_input
 * Mute the local video source (eg. a camera, a data-input or the videostream from a mp4 file)
 * @param client The Pulse handle
 * @param mute TRUE to mute, FALSE to unmute.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_mute_video_input (Pulse * client, bool mute);

/**
 * @brief Get the server version of the conference host
 * This function returns the server version of the connected conference host.
 * @param client The Pulse handle
 * @param major A pointer to a uint32_t type. Upon successful return, the server major version number will be written
 * here.
 * @param minor A pointer to a uint32_t type. Upon successful return, the server minor version number will be written
 * here.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_session_get_server_version (Pulse * client, uint32_t * major, uint32_t * minor);

/**
 * @brief Get the name of the connected conference
 * This function returns a copy of the currently connected conference's name.
 * @param client The Pulse handle
 * @param conference_name A pointer reference to a char * type. Upon successful return, the reference will be set to
 * point to a newly allocated zero terminated buffer containing the conference name.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note Use pulse_session_free_conference_name function to free the participant uuid buffer.
 */
PULSE_EXPORT
PulseError pulse_session_get_conference_name (Pulse * client, char ** conference_name);

/**
 * @brief Free a previously returned conference name.
 * This function frees the memory previously allocated by a call to pulse_session_get_conference_name()
 * @param conference_name A pointer to a string previously retuned from a call to pulse_session_get_conference_name()
 */
PULSE_EXPORT
void pulse_session_free_conference_name (char * conference_name);

/**
 * @brief Get info about the connected conference
 * This function returns info about the currently connected conference's.
 * @param client The Pulse handle
 * @param info_ret A pointer reference to a PulseSessionInfo * type. Upon successful return, the reference will be set
 * to point to object containing the information.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note Use pulse_session_free_conference_info function to free the participant uuid buffer.
 */
PULSE_EXPORT
PulseError pulse_session_get_conference_info (Pulse * client, PulseSessionInfo ** info_ret);

/**
 * @brief Free a previously returned conference info struct.
 * This function frees the memory previously allocated by a call to pulse_session_get_conference_info()
 * @param info A pointer to a string previously retuned from a call to pulse_session_get_conference_info()
 */
PULSE_EXPORT
void pulse_session_free_conference_info (PulseSessionInfo * info);

/**
 * @brief Is the conference allowing full hd video?
 * This function returns true if the conference allows full hd video, and false if disallowed.
 * @param client The Pulse handle
 * @param state A pointer to a bool type. Upon successful return, the result will be written here.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_session_get_allow_1080p (Pulse * client, bool * state);

/**
 * @brief Is the conference allowing the VP9 codec?
 * This function returns true if the conference allows vp9, and false if disallowed.
 * @param client The Pulse handle
 * @param state A pointer to a bool type. Upon successful return, the result will be written here.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_session_get_vp9_enabled (Pulse * client, bool * state);

/**
 * @brief Is the conference chat enabled?
 * This function returns true if the conference is chat enabled, and false if chat is disabled.
 * @param client The Pulse handle
 * @param state A pointer to a bool type. Upon successful return, the result will be written here.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_session_get_chat_enabled (Pulse * client, bool * state);

/**
 * @brief Is your client announced as being FECC enabled?
 * This function returns true if FECC is enabled, and false otherwise.
 * @param client The Pulse handle
 * @param state A pointer to a bool type. Upon successful return, the result will be written here.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_session_get_fecc_enabled (Pulse * client, bool * state);

/**
 * @brief Does the conference allow guests to present?
 * This function returns true if guests are allowed to presend, and false otherwise.
 * @param client The Pulse handle
 * @param state A pointer to a bool type. Upon successful return, the result will be written here.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_session_get_guests_can_present (Pulse * client, bool * state);

/**
 * @brief Is RTMP enabled for the conference?
 * This function returns true if RTMP is enabled, and false otherwise.
 * @param client The Pulse handle
 * @param state A pointer to a bool type. Upon successful return, the result will be written here.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_session_get_rtmp_enabled (Pulse * client, bool * state);

/**
 * @brief Get the client role in the conference.
 * This function returns the clients role in the conferece, either PULSE_CONFERENCE_ROLE_HOST or
 * PULSE_CONFERENCE_ROLE_GUEST.
 * @param client The Pulse handle
 * @param role A pointer to a PulseConferenceRole type. Upon successful return, the result will be written here.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_session_get_role (Pulse * client, PulseConferenceRole * role);

/**
 * @brief Get the host service type.
 * This function the host service type,
 * @param client The Pulse handle
 * @param type A pointer to a PulseConferenceServiceType type. Upon successful return, the result will be written here.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_session_get_service_type (Pulse * client, PulseConferenceServiceType * type);

/**
 * @brief Get the client participant uuid for the conference.
 * This function returns the clients participant uuid.
 * @param client The Pulse handle
 * @param room_id The room id to fetch participant UUID for.
 * @param uuid A pointer reference to a char * type. Upon successful return, the reference will be set to point to a
 * newly allocated zero terminated buffer containing the participant UUID.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note Use pulse_session_free_participant_uuid function to free the participant uuid buffer.
 */

PULSE_EXPORT
PulseError pulse_session_get_participant_uuid (Pulse * client, PulseRoomId room_id, char ** uuid);

/**
 * @brief Free a previously returned participant uuid.
 * This function frees the memory previously allocated by a call to pulse_session_get_participant_uuid()
 * @param uuid A pointer to a participant uuid previously retuned from a call to pulse_session_get_participant_uuid()
 */
PULSE_EXPORT
void pulse_session_free_participant_uuid (char * uuid);

PULSE_EXPORT
PulseError pulse_media_input_main_set_rotation (Pulse * client, PulseMediaRotation rotation);

PULSE_EXPORT
PulseError pulse_media_input_presentation_set_rotation (Pulse * client, PulseMediaRotation rotation);

/**
 * @brief Send message to one or all participants
 * This function sends a message to all participants in the conference.
 * @param client The Pulse handle
 * @param target_participant_uuid A string containing the uuid of the participant to send message to. If NULL, it will
 * be sent to all participants. In a direct media call, this is not used.
 * @param request A pointer to a PulseMessageRequest struct type with all necessary arguments filled in.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_send_message (Pulse * client, const char * target_participant_uuid, PulseMessageRequest * request);

/**
 * @brief Feed an inbound packet into the client.
 *
 * Companion to pulse_options_set_app_transport(). The application calls this function with each
 * packet it has received from its own transport (e.g. a WebSocket tunnel or a UDP socket), to
 * deliver it into the client's receive pipeline.
 *
 * Thread-safe: may be called from any thread, including synchronously from inside the
 * #PulseAppPacketCallback that the client uses for outbound packets (i.e. loopback usage is safe).
 *
 * The @p data buffer is borrowed for the duration of the call; the implementation copies the bytes
 * it needs to retain.
 *
 * Channel id semantics: @p channel_id identifies the wire this inbound packet was received on
 * and must match the channel id that was emitted (or that *would* be emitted) by the corresponding
 * outbound #PulseAppPacketCallback. In bundle mode, a zero-initialised #PulseAppChannelId is
 * the canonical bundle channel id (`{content=MAIN, type=AUDIO, kind=MUX}` — content and type are
 * opaque; only kind matters and must be #PULSE_APP_CHANNEL_KIND_MUX).
 *
 * @param client     The Pulse handle.
 * @param channel_id Identifies the wire this packet was received on.
 * @param data       Pointer to the packet (RTP or RTCP, depending on @p channel_id.kind in split
 *                   mode; an RTP/RTCP-muxed packet in bundle mode).
 * @param size       Length of @p data in bytes.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_app_transport_push (Pulse * client, PulseAppChannelId channel_id, const uint8_t * data, size_t size);

PULSE_DECL_END

#endif /* _PULSE_H_ */
