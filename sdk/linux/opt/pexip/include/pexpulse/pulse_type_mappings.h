/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2022 Pexip AS
 * @author Knut Saastad
 * @author Tulio Beloqui
 */

#ifndef _PULSE_TYPE_MAPPINGS_H_
#define _PULSE_TYPE_MAPPINGS_H_

#include "pulse_conference.h"
#include "pulse_conference_control.h"

PULSE_DECL_BEGIN

/**
 * @brief pulse_type_mapping_debug_level_to_string
 * Converts a PulseDebugLevel to its string representation.
 * @param level The debug level to convert.
 * @return A string representation of the debug level.
 */
PULSE_EXPORT
const char * pulse_type_mapping_debug_level_to_string (PulseDebugLevel level);

/**
 * @brief pulse_type_mapping_connection_status_to_string
 * Converts a PulseConnectionStatus to its string representation.
 * @param status The connection status to convert.
 * @return A string representation of the connection status.
 */
PULSE_EXPORT
const char * pulse_type_mapping_connection_status_to_string (PulseConnectionStatus status);

/**
 * @brief pulse_type_mapping_role_to_string
 * Converts a PulseConferenceRole to its string representation.
 * @param role The conference role to convert.
 * @return A string representation of the role.
 */
PULSE_EXPORT
const char * pulse_type_mapping_role_to_string (PulseConferenceRole role);

/**
 * @brief pulse_type_mapping_role_from_string
 * Converts a string to a PulseConferenceRole.
 * @param role The string to convert.
 * @return The corresponding PulseConferenceRole value.
 */
PULSE_EXPORT
PulseConferenceRole pulse_type_mapping_role_from_string (const char * role);

/**
 * @brief pulse_type_mapping_protocol_to_string
 * Converts a PulseConferenceProtocol to its string representation.
 * @param protocol The conference protocol to convert.
 * @return A string representation of the protocol.
 */
PULSE_EXPORT
const char * pulse_type_mapping_protocol_to_string (PulseConferenceProtocol protocol);

/**
 * @brief pulse_type_mapping_protocol_from_string
 * Converts a string to a PulseConferenceProtocol.
 * @param protocol The string to convert.
 * @return The corresponding PulseConferenceProtocol value.
 */
PULSE_EXPORT
PulseConferenceProtocol pulse_type_mapping_protocol_from_string (const char * protocol);

/**
 * @brief pulse_type_mapping_call_type_to_string
 * Converts a PulseConferenceCallType to its string representation.
 * @param call_type The call type to convert.
 * @return A string representation of the call type.
 */
PULSE_EXPORT
const char * pulse_type_mapping_call_type_to_string (PulseConferenceCallType call_type);

/**
 * @brief pulse_type_mapping_call_type_from_string
 * Converts a string to a PulseConferenceCallType.
 * @param call_type The string to convert.
 * @return The corresponding PulseConferenceCallType value.
 */
PULSE_EXPORT
PulseConferenceCallType pulse_type_mapping_call_type_from_string (const char * call_type);

/**
 * @brief pulse_type_mapping_keep_alive_type_to_string
 * Converts a PulseConferenceKeepAliveType to its string representation.
 * @param keep_alive_type The keep-alive type to convert.
 * @return A string representation of the keep-alive type.
 */
PULSE_EXPORT
const char * pulse_type_mapping_keep_alive_type_to_string (PulseConferenceKeepAliveType keep_alive_type);

/**
 * @brief pulse_type_mapping_keep_alive_type_from_string
 * Converts a string to a PulseConferenceKeepAliveType.
 * @param keep_alive_type The string to convert.
 * @return The corresponding PulseConferenceKeepAliveType value.
 */
PULSE_EXPORT
PulseConferenceKeepAliveType pulse_type_mapping_keep_alive_type_from_string (const char * keep_alive_type);

/**
 * @brief pulse_type_mapping_content_type_to_string
 * Converts a PulseMessageContentType to its string representation.
 * @param content_type The message content type to convert.
 * @return A string representation of the content type.
 */
PULSE_EXPORT
const char * pulse_type_mapping_content_type_to_string (PulseMessageContentType content_type);

/**
 * @brief pulse_type_mapping_content_type_from_string
 * Converts a string to a PulseMessageContentType.
 * @param content_type The string to convert.
 * @return The corresponding PulseMessageContentType value.
 */
PULSE_EXPORT
PulseMessageContentType pulse_type_mapping_content_type_from_string (const char * content_type);

/**
 * @brief pulse_type_mapping_call_direction_to_string
 * Converts a PulseConferenceCallDirection to its string representation.
 * @param call_direction The call direction to convert.
 * @return A string representation of the call direction.
 */
PULSE_EXPORT
const char * pulse_type_mapping_call_direction_to_string (PulseConferenceCallDirection call_direction);

/**
 * @brief pulse_type_mapping_call_direction_from_string
 * Converts a string to a PulseConferenceCallDirection.
 * @param call_direction The string to convert.
 * @return The corresponding PulseConferenceCallDirection value.
 */
PULSE_EXPORT
PulseConferenceCallDirection pulse_type_mapping_call_direction_from_string (const char * call_direction);

/**
 * @brief pulse_type_mapping_rx_presentation_policy_to_string
 * Converts a PulseConferenceRxPresentationPolicy to its string representation.
 * @param presentation_policy The presentation policy to convert.
 * @return A string representation of the presentation policy.
 */
PULSE_EXPORT
const char *
pulse_type_mapping_rx_presentation_policy_to_string (PulseConferenceRxPresentationPolicy presentation_policy);

/**
 * @brief pulse_type_mapping_rx_presentation_policy_from_string
 * Converts a string to a PulseConferenceRxPresentationPolicy.
 * @param presentation_policy The string to convert.
 * @return The corresponding PulseConferenceRxPresentationPolicy value.
 */
PULSE_EXPORT
PulseConferenceRxPresentationPolicy
pulse_type_mapping_rx_presentation_policy_from_string (const char * presentation_policy);

/**
 * @brief pulse_type_mapping_service_type_to_string
 * Converts a PulseConferenceServiceType to its string representation.
 * @param service_type The service type to convert.
 * @return A string representation of the service type.
 */
PULSE_EXPORT
const char * pulse_type_mapping_service_type_to_string (PulseConferenceServiceType service_type);

/**
 * @brief pulse_type_mapping_service_type_from_string
 * Converts a string to a PulseConferenceServiceType.
 * @param service_type The string to convert.
 * @return The corresponding PulseConferenceServiceType value.
 */
PULSE_EXPORT
PulseConferenceServiceType pulse_type_mapping_service_type_from_string (const char * service_type);

/**
 * @brief pulse_type_mapping_bool_from_yes_or_no_string
 * Converts a "yes"/"no" string to a boolean value.
 * @param s The string to convert.
 * @return true if the string is "yes", false otherwise.
 */
PULSE_EXPORT
bool pulse_type_mapping_bool_from_yes_or_no_string (const char * s);

/**
 * @brief pulse_type_mapping_bool_to_yes_or_no_string
 * Converts a boolean value to a "yes" or "no" string.
 * @param state The boolean value to convert.
 * @return "yes" if true, "no" if false.
 */
PULSE_EXPORT
const char * pulse_type_mapping_bool_to_yes_or_no_string (bool state);

/**
 * @brief pulse_type_mapping_bool_from_on_off_string
 * Converts an "on"/"off" string to a boolean value.
 * @param s The string to convert.
 * @return true if the string is "on", false otherwise.
 */
PULSE_EXPORT bool pulse_type_mapping_bool_from_on_off_string (const char * s);

/**
 * @brief pulse_type_mapping_bool_to_on_off_string
 * Converts a boolean value to an "on" or "off" string.
 * @param state The boolean value to convert.
 * @return "on" if true, "off" if false.
 */
PULSE_EXPORT
const char * pulse_type_mapping_bool_to_on_off_string (bool state);

/**
 * @brief pulse_type_mapping_proxy_server_type_to_string
 * Converts a PulseProxyServerType to its string representation.
 * @param type The proxy server type to convert.
 * @return A string representation of the proxy server type.
 */
PULSE_EXPORT
const char * pulse_type_mapping_proxy_server_type_to_string (PulseProxyServerType type);

/**
 * @brief pulse_type_mapping_breakout_room_end_action_to_string
 * Converts a PulseConferenceStatusBreakoutRoomEndAction to its string representation.
 * @param end_action The breakout room end action to convert.
 * @return A string representation of the end action.
 */
PULSE_EXPORT
const char *
pulse_type_mapping_breakout_room_end_action_to_string (PulseConferenceStatusBreakoutRoomEndAction end_action);

/**
 * @brief pulse_type_mapping_breakout_room_end_action_from_string
 * Converts a string to a PulseConferenceStatusBreakoutRoomEndAction.
 * @param end_action The string to convert.
 * @return The corresponding PulseConferenceStatusBreakoutRoomEndAction value.
 */
PULSE_EXPORT
PulseConferenceStatusBreakoutRoomEndAction
pulse_type_mapping_breakout_room_end_action_from_string (const char * end_action);

/**
 * @brief pulse_type_mapping_network_connectivity_level_to_string
 * Converts a PulseNetworkConnectivityLevel to its string representation.
 * @param connectivity_level The network connectivity level to convert.
 * @return A string representation of the connectivity level.
 */
PULSE_EXPORT
const char * pulse_type_mapping_network_connectivity_level_to_string (PulseNetworkConnectivityLevel connectivity_level);

PULSE_DECL_END

#endif