/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2022 Pexip AS
 * @author Knut Saastad
 * @author Tulio Beloqui
 */

#ifndef _PULSE_REGISTRATIONS_H_
#define _PULSE_REGISTRATIONS_H_

#include "pulse_config.h"
#include "pulse_types.h"

PULSE_DECL_BEGIN

typedef struct _PulseRegistrationRequest PulseRegistrationRequest;
typedef struct _PulseRegistrationAlias PulseRegistrationAlias;
typedef struct _PulseRegistrationAliasList PulseRegistrationAliasList;

struct _PulseRegistrationRequest
{
  const char * alias;
  const char * host;
  const char * username;
  const char * password;
  bool use_sso;
};

typedef enum
{
  PULSE_REGISTRATION_ALIAS_DEVICE,
  PULSE_REGISTRATION_ALIAS_CONFERENCE,
} PulseRegistrationAliasType;

struct _PulseRegistrationAlias
{
  PulseRegistrationAliasType type;
  char * alias;
  char * description;
};

struct _PulseRegistrationAliasList
{
  PulseRegistrationAlias ** list;
  size_t size;
};

/**
 * @brief Register as a device to an infinity server. This allows you to do conference/device lookups, and to receive
 * incoming calls. Blocking function to register as a device to an infinity server. This allows you to do
 * conference/device lookups, and to receive incoming calls.
 * @param client The Pulse handle
 * @param registration_request A PulseRegistrationRequest struct type, with the necessary information filled in.
 * @param progress_callback_config An optional callback function with user_context, for receiving progress updates
 * during disconnect.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available when running the Pulse in internal rest handling modes
 */
PULSE_EXPORT
PulseError pulse_register (Pulse * client, PulseRegistrationRequest * registration_request,
                           PulseOperationProgressCallbackConfig * progress_callback_config);

/**
 * @brief Register as a device to an infinity server. This allows you to do conference/device lookups, and to receive
 * incoming calls. Non-blocking function to register as a device to an infinity server. This allows you to do
 * conference/device lookups, and to receive incoming calls.
 * @param client The Pulse handle
 * @param registration_request A PulseRegistrationRequest struct type, with the necessary information filled in.
 * @param result_callback_config Mandatory callback function for receiving the error code from the execution of the
 * async function, once it is done.
 * @param progress_callback_config An optional callback function with user_context, for receiving progress updates
 * during disconnect.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available when running the Pulse in internal rest handling modes
 */
PULSE_EXPORT
PulseError pulse_register_async (Pulse * client, PulseRegistrationRequest * registration_request,
                                 PulseAsyncOperationResultCallbackConfig * result_callback_config,
                                 PulseOperationProgressCallbackConfig * progress_callback_config);

/**
 * @brief De-register a previously registered endpoint.
 * Blocking function to deregister from an infinity server.
 * @param client The Pulse handle
 * @param progress_callback_config An optional callback function with user_context, for receiving progress updates
 * during disconnect.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available when running the Pulse in internal rest handling modes
 */
PULSE_EXPORT
PulseError pulse_deregister (Pulse * client, PulseOperationProgressCallbackConfig * progress_callback_config);

/**
 * @brief De-register a previously registered endpoint.
 * Non-blocking function to deregister from an infinity server.
 * @param client The Pulse handle
 * @param result_callback_config Mandatory callback function for receiving the error code from the execution of the
 * async function, once it is done.
 * @param progress_callback_config An optional callback function with user_context, for receiving progress updates
 * during disconnect.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available when running the Pulse in internal rest handling modes
 */
PULSE_EXPORT
PulseError pulse_deregister_async (Pulse * client, PulseAsyncOperationResultCallbackConfig * result_callback_config,
                                   PulseOperationProgressCallbackConfig * progress_callback_config);

/**
 * @brief Query a previously registered infinity server, for conference and device aliases.
 * Query a previously registered infinity server, for conference and device aliases.
 * @param client The Pulse handle
 * @param query Seach query. Searches are case insensitive and only substring queries are supported, no support for
 * asterisk, regexes or similar.
 * @param device_alias_limit Limit the number of device results. A positive number for setting maximum number of
 * results,  '-1' for unlimited search results, and '0' to disable device alias searches.
 * @param service_alias_limit Limit the number of conference results. A positive number for setting maximum number of
 * results,  '-1' for unlimited search results, and '0' to disable conference alias searches.
 * @param result pointer to PulseRegistrationAliasList *, that will be filled in with the result. The caller is
 * responsible for freeing this after use, by calling pulse_registration_alias_list_free().
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure. PULSE_ERROR_FEATURE_DISABLED is
 * returned if the feature is disabled on server side.
 * @note This function is only available when running the Pulse in internal rest handling modes
 */
PULSE_EXPORT
PulseError pulse_registrations_query_alias (Pulse * client, const char * query, int32_t device_alias_limit,
                                            int32_t service_alias_limit, PulseRegistrationAliasList ** result);

/**
 * @brief Check if the connected infinity registration session, was authenticated using SSO.
 * Check if the connected infinity registration session, was authenticated using SSO.
 * @param client The Pulse handle
 * @param is_sso Reference to a boolean variable, that will have the sso state set, if the call to this function returns
 * successfully.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available when running the Pulse in internal rest handling modes
 */
PULSE_EXPORT
PulseError pulse_registrations_get_is_sso (Pulse * client, bool * is_sso);

/**
 * @brief Check the Infinity version of the connected registration session.
 * Check the Infinity version of the connected registration session.
 * @param client The Pulse handle
 * @param version_major Reference to a uint64_t variable, that will have the Infinity major version set, if the call to
 * this function returns successfully.
 * @param version_minor Reference to a uint64_t variable, that will have the Infinity minor version set, if the call to
 * this function returns successfully.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available when running the Pulse in internal rest handling modes
 */
PULSE_EXPORT
PulseError pulse_registrations_get_server_version (Pulse * client, uint64_t * version_major, uint64_t * version_minor);

/**
 * @brief Get the server side reported display_name for this registration session.
 * Get the server side reported display_name for this registration session.
 * @param client The Pulse handle
 * @param display_name Reference to a char * variable, that will have point to a character buffer holding the
 * display_name, if the call to this function returns successfully. (see note below)
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available when running the Pulse in internal rest handling modes
 * @note Display_name is only available on Infinity V37+, and only for sso enabled calls. Otherwise the returned
 * display_name will be NULL.
 * @note The returned string must be dereferenced by the client, by calling the pulse_registrations_display_name_free()
 * function.
 */
PULSE_EXPORT
PulseError pulse_registrations_get_display_name (Pulse * client, char ** display_name);

/**
 * @brief Get the resolved address of the connected registration server.
 * Get the resolved address of the connected registration server.
 * @param client The Pulse handle
 * @param connected_server Reference to a char * variable, that will have point to a character buffer holding the
 * resolved server address, if the call to this function returns successfully.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 * @note This function is only available when running the Pulse in internal rest handling modes
 * @note The returned string must be dereferenced by the client, by calling the
 * pulse_registrations_connected_server_free() function.
 */
PULSE_EXPORT
PulseError pulse_registrations_get_connected_server (Pulse * client, char ** connected_server);

/**
 * @brief Frees all resources allocated to a PulseRegistrationAliasList.
 * Frees all resources allocated to a PulseRegistrationAliasList.
 * @param ptr The PulseRegistrationAliasList handle to release.
 */
PULSE_EXPORT
void pulse_registration_alias_list_free (PulseRegistrationAliasList * ptr);

PULSE_EXPORT
void pulse_registrations_display_name_free (char * display_name);

PULSE_EXPORT
void pulse_registrations_connected_server_free (char * connected_server);

PULSE_DECL_END

#endif