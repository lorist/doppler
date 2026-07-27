/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2022 Pexip AS
 * @author Knut Saastad
 * @author Tulio Beloqui
 */

#ifndef _PULSE_OPTIONS_H_
#define _PULSE_OPTIONS_H_

#include "pulse_types.h"
#include "pulse_error.h"

PULSE_DECL_BEGIN

/**
 * @brief Set TURN server support enabled state.
 * This function sets TURN server support to whats specified in the enable parameter. TURN server support is enabled by
 * default.
 * @param client The Pulse handle
 * @param enable The new TURN server support state
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_turn_server_support (Pulse * client, bool enable);

/**
 * @brief Enable TURN server support.
 * This function enables TURN server support. TURN server support is enabled by default.
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_enable_turn_server_support (Pulse * client);

/**
 * @brief Disable TURN server support.
 * This function disables TURN server support. TURN server support is enabled by default.
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_disable_turn_server_support (Pulse * client);

/**
 * @brief Set TURN_443 server support enabled state.
 * This function sets TURN_443 server support to whats specified in the enable parameter. TURN_443 server support is
 * enabled by default. Be aware that pulse_options_set_turn_server_support set to FALSE will override this option set to
 * TRUE.
 * @param client The Pulse handle
 * @param enable The new TURN server support state
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_turn_443_server_support (Pulse * client, bool enable);

/**
 * @brief Set STUN server support enabled state.
 * This function sets STUN server support to whats specified in the enable parameter. STUN server support is enabled by
 * default.
 * @param client The Pulse handle
 * @param enable The new STUN server support state
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_stun_server_support (Pulse * client, bool enable);

/**
 * @brief Enable STUN server support.
 * This function enables STUN server support. STUN server support is enabled by default.
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_enable_stun_server_support (Pulse * client);

/**
 * @brief Disable STUN server support.
 * This function disables STUN server support. STUN server support is enabled by default.
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_disable_stun_server_support (Pulse * client);

/**
 * @brief Set IPv6 support enabled state.
 * This function sets IPv6 support to whats specified in the enable parameter. IPv6 server support is enabled by
 * default.
 * @param client The Pulse handle
 * @param enable The new IPv6 support state
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_ipv6_support (Pulse * client, bool enable);

/**
 * @brief Enable IPv6 support.
 * This function enables IPv6 support. IPv6 support is enabled by default.
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_enable_ipv6_support (Pulse * client);

/**
 * @brief Disable IPv6 support.
 * This function disables IPv6 support. IPv6 support is enabled by default.
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_disable_ipv6_support (Pulse * client);

/**
 * @brief CA bundle path.
 * Set filesystem path to where the CA bundle file exists.
 * @param client The Pulse handle
 * @param path Filesystem path
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_ca_bundle_path (Pulse * client, const char * path);

/**
 * @brief Set TLS peer verification enabled state.
 * This function sets TLS peer verification to whats specified in the enable parameter. IPv6 server support is enabled
 * by default.
 * @param client The Pulse handle
 * @param enable The new TLS peer verification state
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_tls_peer_verification (Pulse * client, bool enable);

/**
 * @brief Enable TLS peer verification.
 * This function enables TLS peer verification. TLS peer verification is enabled by default.
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_enable_tls_peer_verification (Pulse * client);

/**
 * @brief Disable TLS peer verification.
 * This function disables TLS peer verification. TLS peer verification is enabled by default.
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_disable_tls_peer_verification (Pulse * client);

/**
 * @brief Set TLS hostname verification enabled state.
 * This function sets TLS hostname verification to whats specified in the enable parameter. TLS hostname verification is
 * enabled by default.
 * @param client The Pulse handle
 * @param enable The new TLS hostname verification state
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_tls_hostname_verification (Pulse * client, bool enable);

/**
 * @brief Enable TLS hostname verification.
 * This function enables TLS hostname verification. TLS host verification is enabled by default.
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_enable_tls_hostname_verification (Pulse * client);

/**
 * @brief Disable TLS hostname verification.
 * This function disables TLS hostname verification. TLS host verification is enabled by default.
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_disable_tls_hostname_verification (Pulse * client);

/**
 * @brief Set verbose logging enabled state.
 * This function sets verbose logging to whats specified in the enable parameter. Verbose logging is disabled by
 * default.
 * @param client The Pulse handle
 * @param enable The new verbose logging state
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_verbose_logging (Pulse * client, bool enable);

/**
 * @brief Enable verbose logging.
 * This function enables verbose logging. Verbose logging is disabled by default.
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_enable_verbose_logging (Pulse * client);

/**
 * @brief Disable verbose logging.
 * This function disables verbose logging. Verbose logging is disabled by default.
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_disable_verbose_logging (Pulse * client);

/**
 * @brief Register version callback.
 * This function registers a callback function for receiving the Infinity version information during connection setup.
 * @param client The Pulse handle
 * @param callback_config A PulseVersionCallbackConfig structure with callback values filled in. User_context
 * can be NULL.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_version_callback (Pulse * client, PulseVersionCallbackConfig * callback_config);

/**
 * @brief Register conference state information callback.
 * This function registers a callback function for receiving conference status updates.
 * @param client The Pulse handle
 * @param callback_config A PulseConferenceStatusCallbackConfig structure with callback values filled in. If the pointer
 * is NULL, the callback function will be disabled.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_conference_state_callback (Pulse * client,
                                                        PulseConferenceStatusCallbackConfig * callback_config);

/**
 * @brief Register registration state information callback.
 * This function registers a callback function for receiving registration status updates.
 * @param client The Pulse handle
 * @param callback_config A PulseRegistrationStatusCallbackConfig structure with callback values filled in. If the
 * pointer is NULL, the callback function will be disabled.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_registration_state_callback (Pulse * client,
                                                          PulseRegistrationStatusCallbackConfig * callback_config);

/**
 * @brief Register network availability state information callback.
 * This function registers a callback function for receiving network availability status updates.
 * @param client The Pulse handle
 * @param callback_config A PulseNetworkStatusCallbackConfig structure with callback values filled in. If the pointer
 * is NULL, the callback function will be disabled.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_network_state_callback (Pulse * client,
                                                     PulseNetworkStatusCallbackConfig * callback_config);

/**
 * @brief Register TLS security downgrade callback.
 * This function registers a callback function for requesting user approval for any TLS security downgrade issues.
 * @param client The Pulse handle
 * @param callback_config A PulseTLSDegradeApprovalCallbackConfig structure with callback values filled in. If the
 * pointer is NULL, the callback function will be disabled.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_tls_degrade_approval_callback (Pulse * client,
                                                            PulseTLSDegradeApprovalCallbackConfig * callback_config);

/**
 * @brief Register PinCode callbacks.
 * This function registers callback functions for dealing with Pin code callbacks.
 * @param client The Pulse handle
 * @param callback_config A PulsePinCodeRequestCallbackConfig structure with callback values filled in. If the pointer
 * is NULL, the callback function will be disabled.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_pin_code_request_callbacks (Pulse * client,
                                                         PulsePinCodeRequestCallbackConfig * callback_config);

/**
 * @brief Register conference extension callback.
 * This function registers callback function for dealing with conference extension callbacks.
 * @param client The Pulse handle
 * @param callback_config A PulseConferenceExtensionRequestCallbackConfig structure with callback values filled in. If
 * the pointer is NULL, the callback function will be disabled.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_conference_extension_request_callback (
  Pulse * client, PulseConferenceExtensionRequestCallbackConfig * callback_config);

/**
 * @brief Register SSO callbacks.
 * This function registers callback functions for dealing with Single Sign-on requests.
 * @param client The Pulse handle
 * @param callback_config A PulseSSOProviderCallbackConfig structure with callback values filled in. If the pointer
 * is NULL, the callback function will be disabled.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_sso_provider_callbacks (Pulse * client, PulseSSOProviderCallbackConfig * callback_config);

/**
 * @brief Register storage callbacks.
 * This function registers callback functions for doing device secure storage.
 * @param client The Pulse handle
 * @param callback_config A PulseStorageCallbackConfig structure with callback values filled in. If the pointer
 * is NULL, the callback function will be disabled.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_storage_callbacks (Pulse * client, PulseStorageCallbackConfig * callback_config);

/**
 * @brief Set the PulseAudioUnmuteApproval callback function and data.
 * This function registers a PulseAudioUnmuteApproval callback.
 * @param client The Pulse handle
 * @param func The callback handler for audio unmute approval callback. If the callback is NULL, the callback function
 * will be disabled.
 * @param user_context An optional user context to retrieve with this callback.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_audio_unmute_approval_callback (Pulse * client, PulseAudioUnmuteApprovalCallback func,
                                                             void * user_context);

/**
 * @brief Set the PulseAudioMuteStateChanged callback function and data.
 * This function registers a PulseAudioMuteStateChanged callback.
 * @param client The Pulse handle
 * @param func The callback handler for audio mute state change callbacks. If the callback is NULL, the callback
 * function will be disabled.
 * @param user_context An optional user context to retrieve with this callback.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_audio_mute_state_changed_callback (Pulse * client, PulseAudioMuteStateChangedCallback func,
                                                                void * user_context);

/**
 * @brief Set the fecc (Far End Camera Control/PTZ) callback.
 * This function registers a fecc callback.
 * @param client The Pulse handle
 * @param func The callback handler for fecc callbacks. If the callback is NULL, the callback function will be disabled.
 * @param user_context An optional user context to retrieve with this callback.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_conference_event_fecc_callback (Pulse * client, PulseConferenceEventFeccCallback func,
                                                             void * user_context);

/**
 * @brief Set the message_received callback.
 * This function registers a message_received callback.
 * @param client The Pulse handle
 * @param func The callback handler for message_received callbacks. If the callback is NULL, the callback function will
 * be disabled.
 * @param user_context An optional user context to retrieve with this callback.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_conference_event_message_received_callback (
  Pulse * client, PulseConferenceEventMessageReceivedCallback func, void * user_context);

/**
 * @brief Set the conference_update callback.
 * This function registers a conference_update callback.
 * @param client The Pulse handle
 * @param func The callback handler for conference_update callbacks. If the callback is NULL, the callback function will
 * be disabled.
 * @param user_context An optional user context to retrieve with this callback.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_conference_event_conference_update_callback (
  Pulse * client, PulseConferenceEventConferenceUpdateCallback func, void * user_context);

/**
 * @brief Set the participant_list_updated callback.
 * This function registers a participant_list_updated callback.
 * @param client The Pulse handle
 * @param func The callback handler for participant_list_updated callbacks. If the callback is NULL, the callback
 * function will be disabled.
 * @param user_context An optional user context to retrieve with this callback.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_conference_event_participant_list_updated_callback (
  Pulse * client, PulseConferenceEventParticipantListUpdatedCallback func, void * user_context);

/**
 * @brief Set the participant_create callback.
 * This function registers a participant_create callback.
 * @param client The Pulse handle
 * @param func The callback handler for participant_create callbacks. If the callback is NULL, the callback function
 * will be disabled.
 * @param user_context An optional user context to retrieve with this callback.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_conference_event_participant_create_callback (
  Pulse * client, PulseConferenceEventParticipantCreateCallback func, void * user_context);

/**
 * @brief Set the participant_delete callback.
 * This function registers a participant_delete callback.
 * @param client The Pulse handle
 * @param func The callback handler for participant_delete callbacks. If the callback is NULL, the callback function
 * will be disabled.
 * @param user_context An optional user context to retrieve with this callback.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_conference_event_participant_delete_callback (
  Pulse * client, PulseConferenceEventParticipantDeleteCallback func, void * user_context);

/**
 * @brief Set the remote_disconnect callback.
 * This function registers a remote_disconnect callback.
 * @param client The Pulse handle
 * @param func The callback handler for remote_disconnect callbacks. If the callback is NULL, the callback function will
 * be disabled.
 * @param user_context An optional user context to retrieve with this callback.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_conference_event_remote_disconnect_callback (
  Pulse * client, PulseConferenceEventRemoteDisconnectCallback func, void * user_context);

/**
 * @brief Set the presentation start callback.
 * This function registers a presentation start callback.
 * @param client The Pulse handle
 * @param func The callback handler for presentation start callbacks. If the callback is NULL, the callback function
 * will be disabled.
 * @param user_context An optional user context to retrieve with this callback.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_conference_event_presentation_start_callback (
  Pulse * client, PulseConferenceEventPresentationStartCallback func, void * user_context);

/**
 * @brief Set the presentation stop callback.
 * This function registers a presentation stop callback.
 * @param client The Pulse handle
 * @param func The callback handler for presentation stop callbacks. If the callback is NULL, the callback function will
 * be disabled.
 * @param user_context An optional user context to retrieve with this callback.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_conference_event_presentation_stop_callback (
  Pulse * client, PulseConferenceEventPresentationStopCallback func, void * user_context);

/**
 * @brief Set the layout callback.
 * This function registers a layout callback.
 * @param client The Pulse handle
 * @param func The callback handler for layout callbacks. If the callback is NULL, the callback function will be
 * disabled.
 * @param user_context An optional user context to retrieve with this callback.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_conference_event_layout_callback (Pulse * client, PulseConferenceEventLayoutCallback func,
                                                               void * user_context);

/**
 * @brief Set the stage callback.
 * This function registers a stage callback.
 * @param client The Pulse handle
 * @param func The callback handler for stage callbacks. If the callback is NULL, the callback function will be
 * disabled.
 * @param user_context An optional user context to retrieve with this callback.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_conference_event_stage_callback (Pulse * client, PulseConferenceEventStageCallback func,
                                                              void * user_context);

/**
 * @brief Set the audio mixer callback.
 * This function registers a audio mixer event callback.
 * @param client The Pulse handle
 * @param func The callback handler for generic callbacks. If the callback is NULL, the callback function will be
 * disabled.
 * @param user_context An optional user context to retrieve with this callback.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_conference_event_audio_mixer_list_callback (Pulse * client,
                                                                         PulseConferenceAudioMixerListCallback func,
                                                                         void * user_context);

/**
 * @brief Set the live_captions callback.
 * This function registers a live_captions callback.
 * @param client The Pulse handle
 * @param func The callback handler for live_captions callbacks. If the callback is NULL, the callback function will be
 * disabled.
 * @param user_context An optional user context to retrieve with this callback.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_conference_event_live_captions_callback (Pulse * client,
                                                                      PulseConferenceEventLiveCaptionsCallback func,
                                                                      void * user_context);

PULSE_EXPORT
PulseError pulse_options_set_breakout_room_pre_transfer_callback (Pulse * client,
                                                                  PulseBreakoutRoomPreTransferCallback func,
                                                                  void * user_context);

PULSE_EXPORT
PulseError pulse_options_set_breakout_room_post_transfer_callback (Pulse * client,
                                                                   PulseBreakoutRoomPostTransferCallback func,
                                                                   void * user_context);

PULSE_EXPORT
PulseError pulse_options_set_breakout_room_transfer_cancelled_callback (Pulse * client,
                                                                        PulseBreakoutRoomTransferCancelledCallback func,
                                                                        void * user_context);

PULSE_EXPORT
PulseError pulse_options_set_breakout_room_created_callback (Pulse * client, PulseBreakoutRoomCreatedCallback func,
                                                             void * user_context);

PULSE_EXPORT
PulseError pulse_options_set_breakout_room_destroyed_callback (Pulse * client, PulseBreakoutRoomDestroyedCallback func,
                                                               void * user_context);

/**
 * @brief Register registrations event callbacks.
 * This function registers callback functions for retrieving registrations events.
 * @param client The Pulse handle
 * @param callback_config A PulseRegistrationsEventCallbackConfig structure with callback values filled in. If the
 * pointer is NULL, the callback function will be disabled.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_registrations_events_callbacks (Pulse * client,
                                                             PulseRegistrationsEventCallbackConfig * callback_config);

/**
 * @brief Set ultra low latency mode state.
 * This function sets ultra low latency mode to whats specified in the enable parameter. Ultra low latency mode is
 * disabled by default.
 * @param client The Pulse handle
 * @param enable The new ultra low latency mode state
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_ultra_low_latency_mode (Pulse * client, bool enable);

/**
 * @brief Enable ultra low latency mode.
 * This function enables ultra low latency mode. Ultra low latency mode is disabled by default.
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_enable_ultra_low_latency_mode (Pulse * client);

/**
 * @brief Disable ultra low latency mode.
 * This function disables ultra low latency mode. Ultra low latency mode is disabled by default.
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_disable_ultra_low_latency_mode (Pulse * client);

/**
 * @brief Set far end camera control mode (FECC) state.
 * This function sets far end camera control mode (FECC) mode to whats specified in the enable parameter. far end camera
 * control mode (FECC) is disabled by default.
 * @param client The Pulse handle
 * @param enable The new far end camera control mode (FECC) state
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_fecc_mode (Pulse * client, bool enable);

/**
 * @brief Set Self view window handle reference.
 * This function sets the self view window handle.
 * @param client The Pulse handle
 * @param window_handle The window handle, or NULL to disable.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_self_view_window_handle (Pulse * client, void * window_handle);

/**
 * @brief Set remote video window handle reference.
 * This function sets the remote video window handle.
 * @param client The Pulse handle
 * @param window_handle The window handle, or NULL to disable.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note call this function with a NULL window_handle, prior to connecting, to disable auto spawning windows.
 */
PULSE_EXPORT
PulseError pulse_options_set_remote_video_window_handle (Pulse * client, void * window_handle);

/**
 * @brief Set presentation video window handle reference.
 * This function sets the presentation video window handle.
 * @param client The Pulse handle
 * @param window_handle The window handle, or NULL to disable.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note call this function with a NULL window_handle, prior to connecting, to disable auto spawning windows.
 */
PULSE_EXPORT
PulseError pulse_options_set_presentation_video_window_handle (Pulse * client, void * window_handle);

/**
 * @brief Set preflight video window handle reference.
 * This function sets the preflight video window handle.
 * @param client The Pulse handle
 * @param window_handle The window handle, or NULL to disable.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note call this function with a NULL window_handle, prior to connecting, to disable auto spawning windows.
 */
PULSE_EXPORT
PulseError pulse_options_set_preflight_video_window_handle (Pulse * client, void * window_handle);

/**
 * @brief Set local RTP portnumber.
 * Set the local RTP portnumber. Applies to both TCP and UPD. Defaults to 0, which makes the client pick one for us.
 * @param client The Pulse handle
 * @param port The port number to use. Valid values are 0 and 1024-65535.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note call this function with a NULL window_handle, prior to connecting, to disable auto spawning windows.
 */
PULSE_EXPORT
PulseError pulse_options_set_local_rtp_port (Pulse * client, uint16_t port);

/**
 * @brief Set local RTCP portnumber.
 * Set the local RTCP portnumber. Applies to both TCP and UPD. Defaults to 0, which makes the client pick one for us.
 * @param client The Pulse handle
 * @param port The port number to use. Valid values are 0 and 1024-65535.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_local_rtcp_port (Pulse * client, uint16_t port);

/**
 * @brief Set backgroud image to be rendered on main output, when not connected to a conference.
 * Tells Pulse to render the image on main local video output when not in a session.
 * @param client The Pulse handle
 * @param image_path Full path to the image. Supported formats are jpg and png.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_background_image (Pulse * client, const char * image_path);

/**
 * @brief Set a proxy server for HTTPS connections.
 * Tells Pulse to proxy all HTTPS calls via a proxy server.
 * @param client The Pulse handle
 * @param config The proxy server connection parameters.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note Call pulse_options_set_proxy_server with a NULL handle to disable a previously set proxy configuration.
 */
PULSE_EXPORT
PulseError pulse_options_set_proxy_server (Pulse * client, const PulseProxyServerConfig * config);

/**
 * @brief Configure the maximum HTTP version to use for connections.!
 * Tells Pulse to not try http version higher than the specified.
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note The connection may end up using a lower version than specified.
 */
PULSE_EXPORT
PulseError pulse_options_set_http_max_version (Pulse * client, PulseHttpVersion);

/**
 * @brief Append a client specific user agent string to our REST queries.
 * Tells Pulse append the provided string to its REST call user agent string.
 * @param client The Pulse handle
 * @param user_agent_string The application-specific string appended to the user agent.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_application_user_agent_string (Pulse * client, const char * user_agent_string);

/**
 * @brief Disable TLS for all http connections, making all traffic unencrypted!
 * Tells Pulse to use unencrypted HTTP communication for all connections.
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This is for testing purposes only and should never be used in a production environment.
 */
PULSE_EXPORT
PulseError pulse_options_disable_https (Pulse * client);

/**
 * @brief Set AGC (automatic gain control) to the main audio stream.
 * This functions sets automatic gain control mode to the main audio stream.
 * @param client The Pulse handle
 * @param enable The new agc mode.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note: Automatic gain control is enabled by default.
 */
PULSE_EXPORT
PulseError pulse_options_set_automatic_gain_control (Pulse * client, bool enable);

/**
 * @brief Apply denoising to the main audio stream.
 * This functions sets denoising mode to the main audio stream.
 * @param client The Pulse handle
 * @param enable The denoising mode.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_denoise (Pulse * client, bool enable);

/**
 * @brief Set background blur to the main video stream.
 * This function segments the participant of the main video stream and blurs the background.
 * @param client The Pulse handle
 * @param enable The background blur mode.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_background_blur (Pulse * client, bool enable);

/**
 * @brief Set video scrambling to the main video stream.
 * This function scrambles the main video stream. Default setting: False.
 * @param client The Pulse handle
 * @param enable The video scrambling mode.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_video_scrambling (Pulse * client, bool enable);

/**
 * @brief Set the sorting preferences for participant list updates.
 * This function sets the sorting preferences for participant list updates.
 * @param client The Pulse handle
 * @param config The sorting config.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_participant_list_sorting (Pulse * client, PulseParticipantListSortingConfig * config);

/**
 * @brief Tells pulse to force route_by_registrar behavior.
 * This function configures pulse to force route_by_registrar behavior. This applies to all connection attempts, meaning
 * a client with no registration will fail all calls.
 * @param client The Pulse handle
 * @param enable The forced_route_by_registrar mode.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note By default, forced_route_by_registrar is disabled.
 */
PULSE_EXPORT
PulseError pulse_options_set_force_route_by_registrar (Pulse * client, bool enable);

/**
 * @brief Enables or disables direct media support.
 * Enables or disables direct media support.
 * @param client The Pulse handle
 * @param enable The direct media allowed mode.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note By default, direct media support is disabled.
 */
PULSE_EXPORT
PulseError pulse_options_set_direct_media_supported (Pulse * client, bool enable);

/**
 * @brief Enables or disables option for allowing direct connection to an FQDN.
 * Enables or disables option for allowing direct connection to a FQDN. This allows
 * connecting to servers that has no SRV records setup correctly.
 * @param client The Pulse handle
 * @param enable The direct media allowed mode.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note By default, direct media support is disabled.
 */
PULSE_EXPORT
PulseError pulse_options_set_allow_direct_fqdn_connect (Pulse * client, bool enable);

/**
 * @brief Enables or disables option for allowing direct connection to an IP address.
 * Enables or disables option for allowing direct connection to an IP address. This is in case the configured
 * server_address is an IPv4/IPv6 address.
 * @param client The Pulse handle
 * @param enable The direct media allowed mode.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note By default, direct media support is disabled.
 */
PULSE_EXPORT
PulseError pulse_options_set_allow_direct_ip_connect (Pulse * client, bool enable);

/**
 * @brief Enable or disable direct chat support
 * Enables or disables direct chat support (disabled by default).
 * @param client The Pulse handle
 * @param enable The direct chat supported mode.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_direct_chat_supported (Pulse * client, bool enable);

/**
 * @brief Set the FQDN of the host to use for DNS probes.
 * Configures what host FQDN to use for DNS probes. Defaults to www.google.com.
 * @param client The Pulse handle
 * @param fqdn Pointer to a string containing the FQDN to use for DNS probes.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note Using an invalid hostname or a hostname with limited scope, will potentialy make connection tests fail.
 */
PULSE_EXPORT
PulseError pulse_options_set_dns_probe_fqdn (Pulse * client, const char * fqdn);

/**
 * @brief Enable or disable Pulse internal filtering of local ICE candidates against our active IP addresses.
 * Filtering is enabled by default unless explicitly disabled by setting @p enable to FALSE.
 * @param client The Pulse handle
 * @param enable If TRUE, filtering is enabled (default). If FALSE, filtering is disabled.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_ice_candidate_active_ip_filtering (Pulse * client, bool enable);

/**
 * @brief Configure application-driven RTP transport.
 *
 * Opts the client into application-driven transport for RTP/RTCP instead of the default
 * UDP transport. Outbound packets are delivered to @p cb tagged with a #PulseAppChannelId;
 * inbound packets are fed back via #pulse_app_transport_push using the same channel id.
 * See #PulseAppChannelId for bundle vs. non-bundle channel-id semantics.
 *
 * Must be called before connection setup begins; later calls return
 * @c PULSE_ERROR_ALREADY_CONNECTED. App-driven transport is mutually exclusive with the
 * default port-/ICE-based transport — pick one before connecting.
 *
 * Once a non-NULL @p cb has been installed the binding is sticky for the lifetime of the
 * client: a subsequent call with @c cb == NULL returns @c PULSE_ERROR_INVALID_PARAMETER.
 * To switch transport mode, destroy the client with @c pulse_free and create a fresh one.
 * When the binding is replaced, the previous @p destroy_cb is invoked; final teardown
 * happens inside @c pulse_free.
 *
 * @param client The Pulse handle.
 * @param cb Outbound packet callback. NULL is allowed only on a client that has never had
 *   a non-NULL binding installed; otherwise the call returns @c PULSE_ERROR_INVALID_PARAMETER.
 *   See #PulseAppPacketCallback for threading and buffer-borrow semantics.
 * @param user_data Opaque pointer passed to @p cb and @p destroy_cb. May be NULL.
 * @param destroy_cb Optional release callback for @p user_data. May be NULL.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_options_set_app_transport (Pulse * client, PulseAppPacketCallback cb, void * user_data,
                                            PulseDestroyCallback destroy_cb);

PULSE_DECL_END

#endif
