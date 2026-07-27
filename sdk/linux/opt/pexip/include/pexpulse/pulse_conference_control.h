/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2022 Pexip AS
 * @author Knut Saastad
 * @author Tulio Beloqui
 */

#ifndef _PULSE_CONFERENCE_CONTROL_H_
#define _PULSE_CONFERENCE_CONTROL_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "pulse_types.h"
#include "pulse_conference.h"
#include "pulse_config.h"
#include "pulse_error.h"

PULSE_DECL_BEGIN

typedef struct _PulseConferenceControlDialRequest PulseConferenceControlDialRequest;
typedef struct _PulseConferenceControlDialResponse PulseConferenceControlDialResponse;
typedef struct _PulseConferenceControlConferenceStatus PulseConferenceControlConferenceStatusResponse;
typedef struct _PulseConferenceEventParticipantList PulseConferenceControlParticipantsResponse;
typedef struct _PulseConferenceControlParticipantAudioMixEntry PulseConferenceControlParticipantAudioMixEntry;
typedef struct _PulseConferenceControlParticipantAudioMixList PulseConferenceControlParticipantAudioMixList;
typedef struct _PulseConferenceControlParticipantVideoMixEntry PulseConferenceControlParticipantVideoMixEntry;
typedef struct _PulseConferenceControlParticipantEntry PulseConferenceControlParticipantEntry;
typedef struct _PulseConferenceControlTransformLayoutRequest PulseConferenceControlTransformLayoutRequest;
typedef struct _PulseConferenceControlTransformLayoutRequestSectionStreaming
  PulseConferenceControlTransformLayoutRequestSectionStreaming;
typedef struct _PulseConferenceControlTransformLayoutRequestSectionFreeFormOverlayText
  PulseConferenceControlTransformLayoutRequestSectionFreeFormOverlayText;
typedef struct _PulseConferenceControlSilentVideoDetectionRequest PulseConferenceControlSilentVideoDetectionRequest;
typedef struct _PulseConferenceClassificationLevel PulseConferenceClassificationLevel;
typedef struct _PulseConferenceClassificationLevelList PulseConferenceClassificationLevelList;
typedef struct _PulseConferenceClassificationInfo PulseConferenceClassificationInfo;
typedef struct _PulseConferenceStatusBreakoutRoom PulseConferenceStatusBreakoutRoom;
typedef struct _PulseConferenceStatusMessageText PulseConferenceStatusMessageText;
typedef struct _PulseConferenceControlBreakoutsParticipantEntry PulseConferenceControlBreakoutsParticipantEntry;
typedef struct _PulseConferenceControlBreakoutsCreateRequest PulseConferenceControlBreakoutsCreateRequest;
typedef struct _PulseConferenceControlBreakoutsTransferRequest PulseConferenceControlBreakoutsTransferRequest;
typedef struct _PulseConferenceControlAvailableLayoutsResponse PulseConferenceControlAvailableLayoutsResponse;
typedef struct _PulseConferenceControlLayoutSvgsResponse PulseConferenceControlLayoutSvgsResponse;
typedef struct _PulseConferenceControlLayoutSvgEntry PulseConferenceControlLayoutSvgEntry;
typedef struct _PulseConferenceControlPinningConfigResponse PulseConferenceControlPinningConfigResponse;
typedef struct _PulseConferenceControlMessageTextResponse PulseConferenceControlMessageTextResponse;
typedef struct _PulseConferenceClassificationInfo PulseConferenceControlClassificationLevelResponse;

typedef enum _PulseConferenceStatusBreakoutRoomEndAction
{
  BREAKOUT_ROOM_END_ACTION_TRANSFER,
  BREAKOUT_ROOM_END_ACTION_DISCONNECT,
} PulseConferenceStatusBreakoutRoomEndAction;

struct _PulseConferenceControlDialRequest
{
  /* Refer to https://docs.pexip.com/api_client/api_rest.htm?Highlight=rest#dial for full descriptions of the struct
   * members. */
  /* Mandatory struct members */
  PulseConferenceRole role;         /* The level of privileges the participant has in the conference. */
  const char * destination;         /* The target address to call. */
  PulseConferenceProtocol protocol; /* The protocol to use to place the outgoing call. */

  /* Optional struct members */
  const char * presentation_url; /* This additional parameter can be specified for RTMP calls to send the presentation
                                    stream to a separate RTMP destination. */
  bool streaming; /* Identifies the dialed participant as a streaming or recording device. [True: streaming/recording
                     participant | False: not a streaming/recording participant]. */
  const char *
    dtmf_sequence; /* An optional DTMF sequence to transmit after the call to the dialed participant starts. */
  const char * source_display_name; /* Optional field that specifies what the calling display name should be. */
  const char * source; /* Optional field that specifies the source URI (must be a valid URI for the conference). */
  PulseConferenceCallType call_type;            /* Optional field that limits the media content of the call. */
  PulseConferenceKeepAliveType keep_alive_type; /* Determines whether the conference continues when all other non-ADP
                                                   participants have disconnected */
  const char * remote_display_name; /* An optional friendly name for this participant. This may be used instead of the
                                       participant's alias in participant lists and as a text overlay in some layout
                                       configurations. */
  const char * text; /* Optional text to use instead of remote_display_name as the participant name overlay text. */
};

struct _PulseConferenceControlDialResponse
{
  /* Refer to https://docs.pexip.com/api_client/api_rest.htm?Highlight=rest#dial for full descriptions of the struct
   * members. */
  size_t uuid_list_size;
  char ** uuid_list;
};

struct _PulseConferenceClassificationLevel
{
  size_t index; /* This is NOT the same as the array index, since index may contain gaps */
  char * description;
};

struct _PulseConferenceClassificationLevelList
{
  size_t list_size;
  PulseConferenceClassificationLevel * list;
};

struct _PulseConferenceClassificationInfo
{
  PulseConferenceClassificationLevelList levels;
  PulseConferenceClassificationLevel * current_level;
};

struct _PulseConferenceStatusBreakoutRoom
{
  char * name;
  char * description;
  PulseConferenceStatusBreakoutRoomEndAction end_action;
  uint64_t end_time;
  bool guests_allowed_to_leave;
  bool buzz;
  uint64_t buzz_time;
};

struct _PulseConferenceStatusMessageText
{
  char * text;
  char * set_time;
};

struct _PulseConferenceControlConferenceStatus
{
  bool locked; /* Conference lock state is hidden for guest participants, and will always be set false */
  bool guests_muted;
  bool all_muted;
  bool presentation_allowed;
  bool started;
  bool live_captions_available;
  bool direct_media;
  bool breakout_rooms_supported;
  PulseConferenceStatusBreakoutRoom * breakout_room;
  char * pinning_config;
  PulseConferenceStatusMessageText * message_text;
  PulseConferenceClassificationInfo * classification;
  bool guests_can_unmute;
  bool guests_can_present;
};

struct _PulseConferenceControlParticipantAudioMixEntry
{
  char * mix_name;
  bool prominent;
};

struct _PulseConferenceControlParticipantAudioMixList
{
  size_t list_size;
  PulseConferenceControlParticipantAudioMixEntry * entries;
};

struct _PulseConferenceControlParticipantVideoMixEntry
{
  char * mix_name;
  double timestamp;
};

struct _PulseConferenceControlParticipantsResponse
{
  size_t participant_list_size;
  PulseConferenceControlParticipantEntry * participant_list;
};

struct _PulseConferenceControlParticipantEntry
{
  uint64_t buzz_time;
  PulseConferenceCallDirection call_direction;
  char * call_tag;
  bool disconnect_supported;
  char * display_name;
  bool encryption;
  char * external_node_uuid;
  bool fecc_supported;
  bool has_media;
  bool is_audio_only_call;
  bool is_external;
  bool is_main_video_dropped_out;
  bool is_muted;
  bool is_presenting;
  bool is_streaming_conference;
  bool is_video_call;
  bool is_video_muted;
  bool is_video_silent;
  bool is_idp_authenticated;
  bool needs_presentation_in_mix;
  bool show_live_captions;
  bool is_conjoined;
  int64_t room_id;
  char * local_alias;
  bool mute_supported;
  char * overlay_text;
  bool presentation_supported;
  PulseConferenceProtocol protocol;
  PulseConferenceRole role;
  PulseConferenceRxPresentationPolicy rx_presentation_policy;
  PulseConferenceServiceType service_type;
  uint64_t spotlight;
  uint64_t start_time;
  bool transfer_supported;
  char * uuid;
  char * uri;
  char * vendor;
  char * api_url;
  char * receive_from_audio_mix;
  PulseConferenceControlParticipantAudioMixList send_to_audio_mixes;
  char * layout_group;
  bool is_client_muted;
  bool is_transferring;
  bool is_local_participant;   /* This is our local participant in the room */
  bool is_active_participant;  /* This participant is activly in our room, and should be visible in roster list */
  bool is_breakout_manageable; /* This participant is active in this breakoutroom, and can be moved. This is only valid
                                  when local_participant role is HOST */
  bool is_pulse_client;        /* This participant is using a pulse based client */
  const char * active_display_name; /* The display name we've sorted on in case of alphabethical sorting. */

  /* Members that are gathered from the stage event */
  bool is_on_stage; /* Participant is in stage list, not sure that really matters */
  bool is_speaking; /* Participant is audible */
  uint32_t vad;     /*range: [0..100]*/

  /* Members that are gathered from the layout event */
  bool is_in_layout; /* Participant is reported to be visible in the video layout */

  /* Infinity v37 members below */
  bool is_ai_enabled;
  bool is_on_hold;
  bool is_public_streaming;
  bool is_recording;
  bool is_transcribing;
  bool supports_direct_chat;
  bool studiosound_enabled;
  bool is_tx_muted;

  /* Infinity v38 members below */
  bool can_receive_personal_mix;
  PulseConferenceControlParticipantVideoMixEntry * receive_from_video_mix;
};

struct _PulseConferenceControlTransformLayoutRequestSectionStreaming
{
  // Not usable if main stream is using Adaptive Composition.
  char * layout;
  bool * waiting_screen_enabled;
  bool * plus_n_pip_enabled;
  bool * indicators_enabled;
};

struct _PulseConferenceControlTransformLayoutRequestSectionFreeFormOverlayText
{
  size_t text_array_size;
  const char ** text_array;
};

struct _PulseConferenceControlTransformLayoutRequest
{
  /* Refer to https://docs.pexip.com/api_client/api_rest.htm?Highlight=rest#transform_layout for full descriptions of
   * the struct members. */
  const char * layout; /* In VMRs the layout for Hosts and Guests is controlled by the layout parameter. Set to NULL for
                          a non VMR configuration. */
  const char * host_layout;  /* Virtual Auditoriums: the Host layout is controlled by the host_layout parameter. Set to
                                NULL for a non Virtual Auditoriums configuration. */
  const char * guest_layout; /* Virtual Auditoriums: the Guest layout is controlled by the guest_layout parameter. Set
                                to NULL for a non Virtual Auditoriums configuration. */
  bool * enable_extended_ac;
  bool * streaming_indicator;
  bool * recording_indicator;
  bool * transcribing_indicator;
  bool * enable_active_speaker_indication;
  bool * enable_overlay_text;
  bool * plus_n_pip_enabled;
  PulseConferenceControlTransformLayoutRequestSectionStreaming * streaming;
  PulseConferenceControlTransformLayoutRequestSectionFreeFormOverlayText * free_form_overlay_text;
};

struct _PulseConferenceControlSilentVideoDetectionRequest
{
  /* Refer to https://docs.pexip.com/api_client/api_rest.htm?Highlight=rest#silentvideo for full descriptions of the
   * struct members. */
  bool
    enable; /* Determines whether silent video detection (no faces or movement) is disabled (false) or enabled (true) */
  uint32_t silent_after; /* The minimum number of seconds the participant's video has to silent before it is considered
                            for removal from the video mix. Valid range 1-120 [Default:15] */
  bool require_no_faces; /* Determines whether or not detected faces in the video are taken into consideration. */
  uint32_t reactivate_after; /* The minimum number of seconds of moving video that is required before marking the
                                participant as no longer silent. Valid range 1-5 [Default:2] */
};

struct _PulseConferenceControlAvailableLayoutsResponse
{
  size_t list_size;
  char ** list;
};

struct _PulseConferenceControlLayoutSvgEntry
{
  char * name;
  char * desc;
};

struct _PulseConferenceControlLayoutSvgsResponse
{
  size_t list_size;
  PulseConferenceControlLayoutSvgEntry * list;
};

struct _PulseConferenceControlPinningConfigResponse
{
  const char * pinning_config;
};

struct _PulseConferenceControlMessageTextResponse
{
  const char * text;
  const char * set_time;
};

struct _PulseConferenceControlBreakoutsParticipantEntry
{
  uint32_t room_id;              /* The room id where this participant exists. Main room is PULSE_ROOM_ID_MAIN */
  const char * participant_uuid; /* The paricipant uuid of the participant in the specified room */
};

struct _PulseConferenceControlBreakoutsCreateRequest
{
  const char * breakout_name; /* The name of the breakout room. */
  uint32_t duration;          /*Optional timer in seconds for how long the breakout is open (0 = indefinitely). */
  PulseConferenceStatusBreakoutRoomEndAction
    end_action; /* What to do with participants when the timer expires, or the breakout is closed. */

  uint64_t transfer_participants_num; /* Number of elements in transfer_participants list. */
  PulseConferenceControlBreakoutsParticipantEntry *
    transfer_participants; /* Array of PulseConferenceControlBreakoutParticipantEntry. */

  bool
    guests_allowed_to_leave; /* Determines whether the guests can return to the main room (before any timer expires). */
};

struct _PulseConferenceControlBreakoutsTransferRequest
{
  PulseRoomId source_room_id;
  PulseRoomId destination_room_id;
  uint64_t transfer_participants_num; /* Number of elements in transfer_participants list. */
  const char **
    transfer_participants; /* Array of char *, containing participant IDs of participants to move to
                              destination_room_id. All participant_uuids must exist in the source_room_id. */
};

/**
 * @brief Dial out from a conference to a target endpoint.
 * This function dials out from the conference to a target endpoint.
 * @param client The Pulse handle
 * @param request A pointer to a PulseConferenceControlDialRequest struct type with all necessary arguments filled in.
 * @param response A pointer reference to a PulseConferenceControlDialResponse struct. If the function returns
 * PULSE_SUCCESS, the reference will point to a newly allocated PulseConferenceControlDialResponse. The caller must
 * later free this result, by calling pulse_conference_control_free_dial_participant_response.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available to conference Hosts.
 * @note It it the responsibility of the caller, to free the data allocated for the PulseConferenceControlDialResponse
 * after use, by calling pulse_conference_control_free_dial_participant_response on the object.
 */
PULSE_EXPORT
PulseError pulse_conference_control_dial (Pulse * client, PulseConferenceControlDialRequest * request,
                                          PulseConferenceControlDialResponse ** response);

/**
 * @brief Retrieve the status of the conference
 * This function return the status of the conference
 * @param client The Pulse handle
 * @param response A pointer reference to a PulseConferenceControlConferenceStatusResponse struct. If the function
 * returns PULSE_SUCCESS, the reference will point to a newly allocated PulseConferenceControlConferenceStatusResponse.
 * The caller must later free this result, by calling pulse_conference_control_free_status_response.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available to conference Hosts.
 * @note It it the responsibility of the caller, to free the data allocated for the
 * PulseConferenceControlConferenceStatusResponse after use, by calling pulse_conference_control_free_status_response on
 * the object.
 */
PULSE_EXPORT
PulseError pulse_conference_control_status (Pulse * client, PulseConferenceControlConferenceStatusResponse ** response);

/**
 * @brief Locks the conference.
 * This function locks the conference
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available to conference Hosts.
 */
PULSE_EXPORT
PulseError pulse_conference_control_lock (Pulse * client);

/**
 * @brief Unlocks the conference.
 * This function unlocks the conference
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available to conference Hosts.
 */
PULSE_EXPORT
PulseError pulse_conference_control_unlock (Pulse * client);

/**
 * @brief Start the conference.
 * This function starts the conference
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available to conference Hosts.
 */
PULSE_EXPORT
PulseError pulse_conference_control_start (Pulse * client);

/**
 * @brief Mute conference guests.
 * This function mutes all conference guests
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available to conference Hosts.
 */
PULSE_EXPORT
PulseError pulse_conference_control_mute_guests (Pulse * client);

/**
 * @brief Unmute conference guests.
 * This function unmutes all conference guests
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available to conference Hosts.
 */
PULSE_EXPORT
PulseError pulse_conference_control_unmute_guests (Pulse * client);

/**
 * @brief Enable soft mute.
 * This function enables soft mute for the conference.
 * @deprecated In version Infinity version 31
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available to conference Hosts.
 */
PULSE_EXPORT
PulseError pulse_conference_control_enable_softmute (Pulse * client);

/**
 * @brief Disable soft mute.
 * This function disables soft mute for the conference.
 * @deprecated In version Infinity version 31
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available to conference Hosts.
 */
PULSE_EXPORT
PulseError pulse_conference_control_disable_softmute (Pulse * client);

/**
 * @brief Disconnect all conference participants.
 * This function disconnects all conference participants, including the participant calling the function.
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available to conference Hosts.
 */
PULSE_EXPORT
PulseError pulse_conference_control_disconnect (Pulse * client);

/**
 * @brief Retrieve conference participant list
 * This function returns the full participant list of the conference.
 * @param client The Pulse handle
 * @param response A pointer reference to a PulseConferenceControlParticipantsResponse struct. Upon successful return,
 * the reference will point to a newly allocated PulseConferenceControlParticipantsResponse. The caller must later free
 * this result, by calling pulse_conference_control_free_participant_list.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available to conference Hosts.
 */
PULSE_EXPORT
PulseError pulse_conference_control_participants (Pulse * client,
                                                  PulseConferenceControlParticipantsResponse ** response);

/**
 * @brief Changes the conference layout, controls streaming content, and enables/disables indicators and overlay text.
 * This function changes the conference layout, controls streaming content, and enables/disables indicators and overlay
 * text..
 * @param client The Pulse handle
 * @param request A pointer to a PulseConferenceControlTransformLayoutRequest struct type with all necessary arguments
 * filled in.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note Calls to this function are applied cumulatively, and struct members with NULL pointers, will remain at their
 * current value. To reset all parameters back to defaults, call with response = NULL.
 * @note This function is only available to conference Hosts.
 */
PULSE_EXPORT
PulseError pulse_conference_control_transform_layout (Pulse * client,
                                                      PulseConferenceControlTransformLayoutRequest * request);

/**
 * @brief Lower all raised hands.
 * This function lowers all raised hands.
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available to conference Hosts.
 */
PULSE_EXPORT
PulseError pulse_conference_control_clearallbuzz (Pulse * client);

/**
 * @brief Configure the parameters for silent video detection in an Adaptive Composition layout.
 * This function configures the parameters for silent video detection in an Adaptive Composition layout.
 * @param client The Pulse handle
 * @param request A pointer to a PulseConferenceControlSilentVideoDetectionRequest struct type with all necessary
 * arguments filled in.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available to conference Hosts.
 */
PULSE_EXPORT
PulseError
pulse_conference_control_silent_video_detection (Pulse * client,
                                                 PulseConferenceControlSilentVideoDetectionRequest * request);

/**
 * @brief Configure if guests are allowed to unmute themselves.
 * This function configures if guests are allowed to unmute themsselves.
 * @param client The Pulse handle
 * @param guests_can_unmute boolean indicating intended unmute setting.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available to conference Hosts.
 */
PULSE_EXPORT
PulseError pulse_conference_control_set_guests_can_unmute (Pulse * client, bool guests_can_unmute);

/**
 * @brief Retrieve a list of available layouts for the conference.
 * This returns a list of all available layouts for the given conference. This includes the inbuilt layouts plus any
 * custom layouts available on this conference.
 * @param client The Pulse handle
 * @param response A pointer reference to a PulseConferenceControlAvailableLayoutsResponse struct. Upon successful
 * return, the reference will point to a newly allocated PulseConferenceControlAvailableLayoutsResponse. The caller must
 * later free this result, by calling pulse_conference_control_free_available_layouts_response.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_conference_control_available_layouts (Pulse * client,
                                                       PulseConferenceControlAvailableLayoutsResponse ** response);

/**
 * @brief Retrieve a list of available layout svgs for the conference.
 * This returns a list of all available layout svgs for the given conference. This includes the inbuilt layouts plus any
 * custom layouts available on this conference.
 * @param client The Pulse handle
 * @param response A pointer reference to a PulseConferenceControlAvailableLayoutsResponse struct. Upon successful
 * return, the reference will point to a newly allocated PulseConferenceControlAvailableLayoutsResponse. The caller must
 * later free this result, by calling pulse_conference_control_free_available_layouts_response.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_conference_control_layout_svgs (Pulse * client, PulseConferenceControlLayoutSvgsResponse ** response);

/**
 * @brief Sets the conference message banner text.
 * Sets the conference message banner text..
 * @param client The Pulse handle
 * @param message_text The banner message text. You can use \n to specify multiple lines. Send an empty string to clear
 * the banner text.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_conference_control_set_message_text (Pulse * client, const char * message_text);

/**
 * @brief Retrieves the current conference message banner text.
 * This retrieves the current conference message banner text.
 * @param client The Pulse handle
 * @param response A pointer reference to a PulseConferenceControlMessageTextResponse struct. Upon successful return,
 * the reference will point to a newly allocated PulseConferenceControlAvailableLayoutsResponse. The caller must later
 * free this result, by calling pulse_conference_control_free_message_text_response.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_conference_control_get_message_text (Pulse * client,
                                                      PulseConferenceControlMessageTextResponse ** response);

/**
 * @brief Sets the pinning configuration for the conference.
 * Sets the pinning configuration for the conference.
 * @param client The Pulse handle
 * @param pinning_config The name of the pinning configuration (from the theme associated with the conference).
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_conference_control_set_pinning_config (Pulse * client, const char * pinning_config);

/**
 * @brief Retrieves the pinning configuration for the conference.
 * This retrieves the pinning configuration for the conference.
 * @param client The Pulse handle
 * @param response A pointer reference to a PulseConferenceControlPinningConfigResponse struct. Upon successful return,
 * the reference will point to a newly allocated PulseConferenceControlAvailableLayoutsResponse. The caller must later
 * free this result, by calling pulse_conference_control_free_pinning_config_response.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_conference_control_get_pinning_config (Pulse * client,
                                                        PulseConferenceControlPinningConfigResponse ** response);

/**
 * @brief Sets the classification level to use from the theme assigned to the conference.
 * Sets the classification level to use from the theme assigned to the conference.
 * @param client The Pulse handle
 * @param classification_level The classification level to use. It must be a valid level key from the theme associated
 * with the conference, retrieved with pulse_conference_control_set_classification_level.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_conference_control_set_classification_level (Pulse * client, uint32_t classification_level);

/**
 * @brief Obtains the set of available classification levels and the currently active level.
 * This obtains the set of available classification levels and the currently active level.
 * @param client The Pulse handle
 * @param response A pointer reference to a PulseConferenceControlClassificationLevelResponse struct. Upon successful
 * return, the reference will point to a newly allocated PulseConferenceControlClassificationLevelResponse. The caller
 * must later free this result, by calling pulse_conference_control_free_classification_level_response.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError
pulse_conference_control_get_classification_level (Pulse * client,
                                                   PulseConferenceControlClassificationLevelResponse ** response);

/**
 * @brief Create a new breakout room.
 * This function creates a new breakout room, and optionally moves specified participants into that room.
 * @param client The Pulse handle
 * @param request The new breakout room configuration.
 * @param response_room_id Reference to a PulseRoomId which will be filled in with the id of the new room.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available to conference Hosts.
 */
PULSE_EXPORT
PulseError pulse_conference_control_breakout_create (Pulse * client,
                                                     PulseConferenceControlBreakoutsCreateRequest * request,
                                                     PulseRoomId * response_room_id);

/**
 * @brief Disconnect/close a breakout room.
 * This function close a breakout room. Depending on the end_action specified on creation, participants will be
 * transferred back to the main room or disconnected.
 * @param client The Pulse handle
 * @param room_id the PulseRoomId that will be closed
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available to conference Hosts.
 */
PULSE_EXPORT
PulseError pulse_conference_control_breakout_close (Pulse * client, PulseRoomId room_id);

/**
 * @brief Empty all breakout rooms, and move participants back to "main". The breakoutrooms are kept running.
 * This function moves all participants into the main room, but does not shut down the breakout rooms.
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available to conference Hosts.
 */
PULSE_EXPORT
PulseError pulse_conference_control_breakouts_empty (Pulse * client);

/**
 * @brief Requests help for the current breakout room.
 * This function requests help for the current breakout room.
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_conference_control_breakouts_buzz (Pulse * client);

/**
 * @brief Cancel a request help for the current breakout room.
 * This function cancels a request for help for the current breakout room.
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_conference_control_breakouts_clearbuzz (Pulse * client);

/**
 * @brief Transfer participants to another room.
 * This function moves all participants into the main room, but does not shut down the breakout rooms.
 * @param client The Pulse handle
 * @param request The transfer request describing the source/target rooms and participants.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available to conference Hosts.
 */
PULSE_EXPORT
PulseError
pulse_conference_control_breakouts_transfer_participants (Pulse * client,
                                                          PulseConferenceControlBreakoutsTransferRequest * request);

/**
 * @brief If guests are allowed to leave the breakout room, a participant can use leavebreakout to transfer back to the
 * main room This function allows a guest to leave the breakoutroom.
 * @param client The Pulse handle
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available to conference Guests.
 */
PULSE_EXPORT
PulseError pulse_conference_control_breakouts_leavebreakout (Pulse * client);

/**
 * @brief Free PulseConferenceControlDialResponse
 * This function frees any memory associated with a PulseConferenceControlDialResponse object.
 * @param ptr A pointer to a PulseConferenceControlDialResponse object.
 */
PULSE_EXPORT
void pulse_conference_control_free_dial_participant_response (PulseConferenceControlDialResponse * ptr);

/**
 * @brief Free PulseConferenceControlParticipantsResponse
 * This function frees any memory associated with a PulseConferenceControlParticipantsResponse object.
 * @param ptr A pointer to a PulseConferenceControlParticipantsResponse object.
 */
PULSE_EXPORT
void pulse_conference_control_free_participant_list (PulseConferenceControlParticipantsResponse * ptr);

/**
 * @brief Free PulseConferenceAudioMixersList
 * This function frees any memory associated with a PulseConferenceAudioMixersList object.
 * @param list A pointer to a PulseConferenceAudioMixersList object.
 */
PULSE_EXPORT
void pulse_conference_control_free_audio_mixers_list (PulseConferenceAudioMixersList * list);

PULSE_EXPORT
void pulse_conference_control_free_status_response (PulseConferenceControlConferenceStatusResponse * ptr);

PULSE_EXPORT
void
pulse_conference_control_free_classification_level_response (PulseConferenceControlClassificationLevelResponse * ptr);

/**
 * @brief Free PulseConferenceControlAvailableLayoutsResponse
 * This function frees any memory associated with a PulseConferenceControlAvailableLayoutsResponse object.
 * @param ptr A pointer to a PulseConferenceControlAvailableLayoutsResponse object.
 */
PULSE_EXPORT
void pulse_conference_control_free_available_layouts_response (PulseConferenceControlAvailableLayoutsResponse * ptr);

/**
 * @brief Free PulseConferenceControlLayoutSvgsResponse
 * This function frees any memory associated with a PulseConferenceControlLayoutSvgsResponse object.
 * @param ptr A pointer to a PulseConferenceControlLayoutSvgsResponse object.
 */
PULSE_EXPORT
void pulse_conference_control_free_layout_svgs_response (PulseConferenceControlLayoutSvgsResponse * ptr);

/**
 * @brief Free PulseConferenceControlMessageTextResponse
 * This function frees any memory associated with a PulseConferenceControlMessageTextResponse object.
 * @param ptr A pointer to a PulseConferenceControlMessageTextResponse object.
 */
PULSE_EXPORT
void pulse_conference_control_free_message_text_response (PulseConferenceControlMessageTextResponse * ptr);

/**
 * @brief Free PulseConferenceControlPinningConfigResponse
 * This function frees any memory associated with a PulseConferenceControlPinningConfigResponse object.
 * @param ptr A pointer to a PulseConferenceControlPinningConfigResponse object.
 */
PULSE_EXPORT
void pulse_conference_control_free_pinning_config_response (PulseConferenceControlPinningConfigResponse * ptr);

PULSE_DECL_END

#endif
