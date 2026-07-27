/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2022 Pexip AS
 * @author Knut Saastad
 * @author Tulio Beloqui
 */

#ifndef _PULSE_PARTICIPANT_FUNCTIONS_H_
#define _PULSE_PARTICIPANT_FUNCTIONS_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "pulse_config.h"
#include "pulse_error.h"

PULSE_DECL_BEGIN

typedef struct _PulseParticipantControlAvatarJpgResponse PulseParticipantControlAvatarJpgResponse;

struct _PulseParticipantControlAvatarJpgResponse
{
  size_t image_buffer_size; /* Size of Image buffer (bytes) */
  uint8_t * image_buffer;   /* Image buffer */
};

/**
 * @brief Disconnect a participant.
 * This functions disconnects the participant identified by the target_participant_uuid, from the conference.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function can only be called when the session has role HOST.
 */
PULSE_EXPORT
PulseError pulse_participant_control_disconnect (Pulse * client, const char * target_participant_uuid);

/**
 * @brief Audio mute a remote participant.
 * This functions mutes/unmutes audio input from the participant identified by the target_participant_uuid.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant. Set to NULL for targeting this client.
 * @param mute boolean indicating the new mute state for the targeted participant, true to mute or false to unmute.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note Cannot be used to alter mute state of  the local participant. See pulse_mute_audio_input()
 * @note This function can only be called when the session has role HOST.
 */
PULSE_EXPORT
PulseError pulse_participant_control_remote_audio_mute (Pulse * client, const char * target_participant_uuid,
                                                        bool mute);

/**
 * @brief Video mute a remote participant.
 * This functions mutes/unmutes video input from the participant identified by the target_participant_uuid.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant. Set to NULL for targeting this client.
 * @param mute boolean indicating the new mute state for the targeted participant, true to mute or false to unmute.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note Cannot be used to alter mute state of  the local participant. See pulse_mute_video_input()
 * @note This function can only be called when the session has role HOST.
 */
PULSE_EXPORT
PulseError pulse_participant_control_remote_video_mute (Pulse * client, const char * target_participant_uuid,
                                                        bool mute);

/**
 * @brief Allow participant to receive presentation stream.
 * This function enables the participant identified by the target_participant_uuid, to receiving the presentation
 * stream.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function can only be called when the session has role HOST.
 */
PULSE_EXPORT
PulseError pulse_participant_control_allow_rx_presentation (Pulse * client, const char * target_participant_uuid);

/**
 * @brief Deny participant to receive presentation stream.
 * This function disable the participant identified by the target_participant_uuid, from receiving the presentation
 * stream.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function can only be called when the session has role HOST.
 */
PULSE_EXPORT
PulseError pulse_participant_control_deny_rx_presentation (Pulse * client, const char * target_participant_uuid);

/**
 * @brief Set spotlight on participant.
 * This functions spotlights the participant identified by the target_participant_uuid.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function can only be called when the session has role HOST.
 */
PULSE_EXPORT
PulseError pulse_participant_control_spotlight_on (Pulse * client, const char * target_participant_uuid);

/**
 * @brief Unset spotlight on participant.
 * This functions turns off spotlight on the participant identified by the target_participant_uuid.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function can only be called when the session has role HOST.
 */
PULSE_EXPORT
PulseError pulse_participant_control_spotlight_off (Pulse * client, const char * target_participant_uuid);

/**
 * @brief Unlock a participant.
 * This functions lets the participant identified by the target_participant_uuid, into the conference from the waiting
 * room of a locked conference.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function can only be called when the session has role HOST.
 */
PULSE_EXPORT
PulseError pulse_participant_control_unlock (Pulse * client, const char * target_participant_uuid);

/**
 * @brief Send DTMF digits to participant.
 * This function sends DTMF digits to the participant identified by the target_participant_uuid.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant identified by the target_participant_uuid.
 * @param dtmf_digits Pointer to a zero terminated char buffer containing the DTMF digits to send.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note Target_participant_uuid is ignored for gateway calls.
 * @note This function can only be called when the session has role HOST.
 */
PULSE_EXPORT
PulseError pulse_participant_control_dtmf (Pulse * client, const char * target_participant_uuid,
                                           const char * dtmf_digits);

/**
 * @brief Change the participant name overlay text.
 * This function changes the overlay text of the participant identified by the target_participant_uuid.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant. Set to NULL for targeting this client.
 * @param overlay_text The new overlay text to display for the target participant.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note If target_participant_uuid is NULL (targeting this client), this can be called regardless of session role.
 * @note If target is specified, this function can only be called when the session has role HOST.
 */
PULSE_EXPORT
PulseError pulse_participant_control_overlay_text (Pulse * client, const char * target_participant_uuid,
                                                   const char * overlay_text);

/**
 * @brief Controls whether or not the participant sees presentation in the layout mix (Adaptive Composition layout
 * only). This function controls whether or not the participant identified by the target_participant_uuid, sees
 * presentation in the layout mix.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant. Set to NULL for targeting this client.
 * @param presentation_in_mix TRUE to show presentation in the layout mix, FALSE to hide it.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note If target_participant_uuid is NULL (targeting this client), this can be called regardless of session role.
 * @note If target is specified, this function can only be called when the session has role HOST.
 */
PULSE_EXPORT
PulseError pulse_participant_control_presentation_in_mix (Pulse * client, const char * target_participant_uuid,
                                                          bool presentation_in_mix);

/**
 * @brief Change the participant role
 * Changes the role of the participant identified by the target_participant_uuid.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant
 * @param role PULSE_CONFERENCE_ROLE_HOST or PULSE_CONFERENCE_ROLE_GUEST
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function can only be called when the session has role HOST.
 */
PULSE_EXPORT
PulseError pulse_participant_control_role (Pulse * client, const char * target_participant_uuid,
                                           PulseConferenceRole role);

/**
 * @brief Send Far end camera control (FECC) to the participant.
 * Send Far End Camera Control messaging to the participant identified by the target_participant_uuid.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant
 * @param action A PulseFeccActionType, one of [PULSE_FECC_ACTION_START, PULSE_FECC_ACTION_CONTINUE,
 * PULSE_FECC_ACTION_STOP]
 * @param movement A PulseFeccMovementDirection mask containing on or more camera controls.
 * @param timeout_ms Timeout in milliseconds for the FECC command to be acknowledged.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function can only be called when the session has role HOST.
 */
PULSE_EXPORT
PulseError pulse_participant_control_fecc (Pulse * client, const char * target_participant_uuid,
                                           PulseFeccActionType action, PulseFeccMovementDirection movement,
                                           uint32_t timeout_ms);

/**
 * @brief Raise a participant's hand.
 * This functions raises the hand of the participant identified by the target_participant_uuid.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant. Set to NULL for targeting this client.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note Target_participant_uuid is ignored for gateway calls.
 * @note If target_participant_uuid is NULL (targeting this client), this can be called regardless of session role.
 * @note If target is specified, this function can only be called when the session has role HOST.
 */
PULSE_EXPORT
PulseError pulse_participant_control_buzz (Pulse * client, const char * target_participant_uuid);

/**
 * @brief Lower a participant's hand.
 * This functions lowers the hand of the participant identified by the target_participant_uuid.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant. Set to NULL for targeting this client.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note If target_participant_uuid is NULL (targeting this client), this can be called regardless of session role.
 * @note If target is specified, this function can only be called when the session has role HOST.
 */
PULSE_EXPORT
PulseError pulse_participant_control_clearbuzz (Pulse * client, const char * target_participant_uuid);

/**
 * @brief Transfers a participant to another conference.
 * This function transfers a participant identified by the target_participant_uuid, to another conference.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant
 * @param role PULSE_CONFERENCE_ROLE_HOST or PULSE_CONFERENCE_ROLE_GUEST
 * @param conference_alias Pointer to a zero terminated char buffer containing the target conference alias.
 * @param pin Pointer to a zero terminated char buffer containing the PIN code for the specified role at the specified
 * conference. Can be NULL if not required.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function can only be called when the session has role HOST.
 */
PULSE_EXPORT
PulseError pulse_participant_control_transfer (Pulse * client, const char * target_participant_uuid,
                                               PulseConferenceRole role, const char * conference_alias,
                                               const char * pin);

/**
 * @brief Signals that the participant is starting sending full-motion presentation.
 * This function signals that the participant identified by the target_participant_uuid, is starting sending full-motion
 * presentation.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant. Set to NULL for targeting this client.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note If target_participant_uuid is NULL (targeting this client), this can be called regardless of session role.
 * @note If target is specified, this function can only be called when the session has role HOST.
 */
PULSE_EXPORT
PulseError pulse_participant_control_take_floor (Pulse * client, const char * target_participant_uuid);

/**
 * @brief Signals that the participant has finished sending full-motion presentation.
 * This function signals that the participant identified by the target_participant_uuid, has finished sending
 * full-motion presentation.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant. Set to NULL for targeting this client.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note If target_participant_uuid is NULL (targeting this client), this can be called regardless of session role.
 * @note If target is specified, this function can only be called when the session has role HOST.
 */
PULSE_EXPORT
PulseError pulse_participant_control_release_floor (Pulse * client, const char * target_participant_uuid);

/**
 * @brief Obtains the image to display to represent a conference participant or directory contact.
 * This function obtains the image to display to represent a conference participant or directory contact.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant. Set to NULL for targeting this client.
 * @param avatar_response A pointer pointer to a PulseParticipantControlAvatarJpgResponse. It is the responsibility of
 * the caller to free this data after use, by calling pulse_participant_control_free_avatar_jpg_response()
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note If target_participant_uuid is NULL (targeting this client), this can be called regardless of session role.
 * @note If target is specified, this function can only be called when the session has role HOST.
 */
PULSE_EXPORT
PulseError pulse_participant_control_avatar_jpg (Pulse * client, const char * target_participant_uuid,
                                                 PulseParticipantControlAvatarJpgResponse ** avatar_response);

/**
 * @brief Request a specific aspect ratio from infinity.
 * This function informs infinity that the client wishes to receive main video with a certain aspect ratio.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant. Set to NULL for targeting this client.
 * @param aspect_ratio A float specifying the new preferred aspect ratio. Valid range <0,2]
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note If target_participant_uuid is NULL (targeting this client), this can be called regardless of session role.
 * @note If target is specified, this function can only be called when the session has role HOST.
 */
PULSE_EXPORT
PulseError pulse_participant_control_preferred_aspect_ratio (Pulse * client, const char * target_participant_uuid,
                                                             float aspect_ratio);

/**
 * @brief Request a specific aspect ratio from infinity.
 * This function informs infinity that the client wishes to receive main video with a certain aspect ratio.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant. Set to NULL for targeting this client.
 * @param width The width of the new preferred aspect.
 * @param height The height of the new preferred aspect.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note If target_participant_uuid is NULL (targeting this client), this can be called regardless of session role.
 * @note If target is specified, this function can only be called when the session has role HOST.
 */
PULSE_EXPORT
PulseError pulse_participant_control_preferred_aspect_ratio_from_size (Pulse * client,
                                                                       const char * target_participant_uuid,
                                                                       uint32_t width, uint32_t height);

/**
 * @brief Assigns a participant to a layout group.
 * This function assigns a participant to a layout group.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant. Set to NULL for targeting this client.
 * @param layout_group a string indicating the layout grounp to assign this participant to.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note If target_participant_uuid is NULL (targeting this client), this can be called regardless of session role.
 * @note If target is specified, this function can only be called when the session has role HOST.
 */
PULSE_EXPORT
PulseError pulse_participant_control_layout_group (Pulse * client, const char * target_participant_uuid,
                                                   const char * layout_group);

/**
 * @brief Request that the audio the participant sends, is going to a specific mixer.
 * Using this function, a participant is able to provide a live audio interpretation of the ongoing conference which
 * other participants are able to listen to.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant. Set to NULL for targeting this client.
 * @param mix_name Name of the audio-mixer that we will send this participants audio to.
 * @param prominent If set then this participant will be louder than others in the mix when speaking (all non-prominent
 * speakers will be “ducked” and made quieter).
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note If target_participant_uuid is NULL (targeting this client), this can be called regardless of session role.
 * @note If target is specified, this function can only be called when the session has role HOST.
 */
PULSE_EXPORT
PulseError pulse_participant_control_send_to_audio_mix (Pulse * client, const char * target_participant_uuid,
                                                        const char * mix_name, bool prominent);

/**
 * @brief Request that the audio the participant receives, is coming from a specific mixer.
 * Using this function, a participant can chose to listen to an alternative audio mix, which eg. can be a live audio
 * interpretation of the ongoing conference.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant. Set to NULL for targeting this client.
 * @param mix_name Name of the audio-mixer that we will send this participants audio to.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note If target_participant_uuid is NULL (targeting this client), this can be called regardless of session role.
 * @note If target is specified, this function can only be called when the session has role HOST.
 */
PULSE_EXPORT
PulseError pulse_participant_control_receive_from_audio_mix (Pulse * client, const char * target_participant_uuid,
                                                             const char * mix_name);

/**
 * @brief Show live captions.
 * Using this function, a participant can chose to receive live captions for the conference.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant. Set to NULL for targeting this client.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note If target_participant_uuid is NULL (targeting this client), this can be called regardless of session role.
 * @note If target is specified, this function can only be called when the session has role HOST.
 * @note THIS IS A BETA FEATURE! Not garanteed to work on all platforms, and API may change at any time.
 */
PULSE_EXPORT
PulseError pulse_participant_control_show_live_captions (Pulse * client, const char * target_participant_uuid);

/**
 * @brief Hide live captions.
 * Using this function, a participant can stop receiving live captions for the conference.
 * @param client The Pulse handle
 * @param target_participant_uuid UUID of the target participant. Set to NULL for targeting this client.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note If target_participant_uuid is NULL (targeting this client), this can be called regardless of session role.
 * @note If target is specified, this function can only be called when the session has role HOST.
 * @note THIS IS A BETA FEATURE! Not garanteed to work on all platforms, and API may change at any time.
 */
PULSE_EXPORT
PulseError pulse_participant_control_hide_live_captions (Pulse * client, const char * target_participant_uuid);

/**
 * @brief Retrieves the avatar image size (in bytes)
 * Retrieves the avatar image size (in bytes) of the referenced PulseParticipantControlAvatarJpgResponse
 * @param ptr Pointer to a previously allocated PulseParticipantControlAvatarJpgResponse type.
 */
PULSE_EXPORT
size_t pulse_participant_control_avatar_jpg_get_image_buf_size (PulseParticipantControlAvatarJpgResponse * ptr);

/**
 * @brief Retrieves the avatar image buffer
 * Retrieves the avatar image buffer of the referenced PulseParticipantControlAvatarJpgResponse
 * @param ptr Pointer to a previously allocated PulseParticipantControlAvatarJpgResponse type.
 */
PULSE_EXPORT
const uint8_t * pulse_participant_control_avatar_jpg_get_image_buf (PulseParticipantControlAvatarJpgResponse * ptr);

/**
 * @brief Frees the content of PulseParticipantControlAvatarJpgResponse
 * Frees the content of PulseParticipantControlAvatarJpgResponse
 * @param ptr Pointer to a previously allocated PulseParticipantControlAvatarJpgResponse type.
 */
PULSE_EXPORT
void pulse_participant_control_free_avatar_jpg_response (PulseParticipantControlAvatarJpgResponse * ptr);

PULSE_DECL_END

#endif