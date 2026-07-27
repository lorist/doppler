/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2022 Pexip AS
 * @author Knut Saastad
 * @author Tulio Beloqui
 */

#ifndef _PULSE_TYPES_H_
#define _PULSE_TYPES_H_

#include <stdbool.h>
#include <stddef.h>
#include "pulse_config.h"
#include "pulse_media.h"
#include "pulse_conference.h"
#include "pulse_conference_event.h"
#include "pulse_registrations_event.h"
#include "pulse_fecc_types.h"
#include "pulse_error.h"
#include "pulse_breakout_rooms.h"

PULSE_DECL_BEGIN

// Opaque types
typedef struct _Pulse Pulse;
typedef struct _PulseRestServerQuery PulseRestServerQuery;

// Transparent types
typedef struct _PulseOperationProgressCallbackConfig PulseOperationProgressCallbackConfig;
typedef struct _PulseOperationProgressInfo PulseOperationProgressInfo;

typedef struct _PulseVersionCallbackConfig PulseVersionCallbackConfig;
typedef struct _PulseConferenceStatusCallbackConfig PulseConferenceStatusCallbackConfig;
typedef struct _PulseConferenceStatusInfo PulseConferenceStatusInfo;
typedef struct _PulseRegistrationStatusCallbackConfig PulseRegistrationStatusCallbackConfig;
typedef struct _PulseRegistrationStatusInfo PulseRegistrationStatusInfo;

typedef struct _PulseNetworkStatusCallbackConfig PulseNetworkStatusCallbackConfig;
typedef struct _PulseNetworkStatusInfo PulseNetworkStatusInfo;

typedef struct _PulsePinCodeRequestCallbackConfig PulsePinCodeRequestCallbackConfig;
typedef struct _PulseSetPinCode PulseSetPinCode;

typedef struct _PulseConferenceExtensionRequestCallbackConfig PulseConferenceExtensionRequestCallbackConfig;
typedef struct _PulseSetConferenceExtension PulseSetConferenceExtension;

typedef struct _PulseExternalRestCallbackConfig PulseExternalRestCallbackConfig;

typedef struct _PulseSSOProvider PulseSSOProvider;
typedef struct _PulseSSOProviderList PulseSSOProviderList;
typedef struct _PulseSSOProviderRequest PulseSSOProviderRequest;
typedef struct _PulseSSOProviderCallbackConfig PulseSSOProviderCallbackConfig;
typedef struct _PulseStorageCallbackConfig PulseStorageCallbackConfig;
typedef struct _PulseSSOProviderSetToken PulseSSOProviderSetToken;
typedef struct _PulseTLSDegradeApprovalCallbackConfig PulseTLSDegradeApprovalCallbackConfig;
typedef struct _PulseRestConnectionConfig PulseRestConnectionConfig;
typedef struct _PulseTurnConfig PulseTurnConfig;
typedef struct _PulseStunConfig PulseStunConfig;
typedef struct _PulseSetupStage1Config PulseSetupStage1Config;
typedef struct _PulseSetupStage2Config PulseSetupStage2Config;
typedef struct _PulseMediaStats PulseMediaStats;
typedef struct _PulseMediaRxStats PulseMediaRxStats;
typedef struct _PulseMediaTxStats PulseMediaTxStats;

typedef struct _PulseProxyServerConfig PulseProxyServerConfig;

typedef struct _PulseMediaDebugData PulseMediaDebugData;
typedef struct _PulseInfo PulseInfo;
typedef struct _PulseSessionInfo PulseSessionInfo;
typedef struct _PulseConferenceStatusBreakoutRoom PulseSessionInfoBreakoutRoom;

typedef struct _PulseConferenceEventLayoutCoordinates PulseConferenceEventLayoutCoordinates;
typedef struct _PulseConferenceEventLayoutPresSlotCoords PulseConferenceEventLayoutPresSlotCoords;
typedef struct _PulseConferenceEventLayout PulseConferenceEventLayout;

typedef struct _PulseConferenceEventStageSpeaker PulseConferenceEventStageSpeaker;
typedef struct _PulseConferenceEventStage PulseConferenceEventStage;
typedef struct _PulseConferenceEventLiveCaptions PulseConferenceEventLiveCaptions;

typedef struct _PulseConferenceEventGeneric PulseConferenceEventGeneric;
typedef struct _PulseConferenceEventFecc PulseConferenceEventFecc;
typedef struct _PulseConferenceEventMessageReceived PulseConferenceEventMessageReceived;
typedef struct _PulseConferenceEventPresentationStart PulseConferenceEventPresentationStart;

typedef struct _PulseConferenceControlConferenceStatus PulseConferenceEventConferenceUpdate;
typedef struct _PulseConferenceEventParticipantList PulseConferenceEventParticipantList;
typedef struct _PulseConferenceEventParticipantCreate PulseConferenceEventParticipantCreate;
typedef struct _PulseConferenceEventParticipantDelete PulseConferenceEventParticipantDelete;
typedef struct _PulseConferenceAudioMixersList PulseConferenceAudioMixersList;

typedef struct _PulseRegistrationsEventCallbackConfig PulseRegistrationsEventCallbackConfig;
typedef struct _PulseRegistrationsEventIncoming PulseRegistrationsEventIncoming;
typedef struct _PulseRegistrationsEventIncomingCancelled PulseRegistrationsEventIncomingCancelled;

typedef struct _PulseAsyncOperationResultCallbackConfig PulseAsyncOperationResultCallbackConfig;

typedef struct _PulseMessageRequest PulseMessageRequest;

typedef struct _PulseIPCHandle PulseIPCHandle;

typedef struct _PulseParticipantListSortingConfig PulseParticipantListSortingConfig;

typedef struct _PulseTuneables PulseTuneables;

typedef enum
{
  PULSE_LEVEL_NONE = 0,
  PULSE_LEVEL_ERROR = 1,
  PULSE_LEVEL_WARNING = 2,
  PULSE_LEVEL_FIXME = 3,
  PULSE_LEVEL_INFO = 4,
  PULSE_LEVEL_DEBUG = 5,
  PULSE_LEVEL_LOG = 6,
  PULSE_LEVEL_TRACE = 7,
} PulseDebugLevel;

typedef enum
{
  PULSE_CONNECTION_STATUS_DISCONNECTED = 0,
  PULSE_CONNECTION_STATUS_CONNECTING,
  PULSE_CONNECTION_STATUS_RECONNECTING,
  PULSE_CONNECTION_STATUS_CONNECTED,
  PULSE_CONNECTION_STATUS_DISCONNECTING,
} PulseConnectionStatus;

typedef enum
{
  PULSE_SSO_REQUEST_CONFERENCE,
  PULSE_SSO_REQUEST_REGISTRATION,
} PulseSSORequestType;

/* Note: not all of these are actively used on all platforms. Some platforms only gather info for LOCAL and FULL */
typedef enum
{
  PULSE_NETWORK_CONNECTIVITY_UNKNOWN = 0,
  PULSE_NETWORK_CONNECTIVITY_LOCAL = 1,
  PULSE_NETWORK_CONNECTIVITY_LIMITED = 2,
  PULSE_NETWORK_CONNECTIVITY_PORTAL = 3,
  PULSE_NETWORK_CONNECTIVITY_FULL = 4
} PulseNetworkConnectivityLevel;

typedef uint64_t PulseTimestamp;

#define PULSE_TIMESTAMP_NONE (uint64_t)-1

// Function pointer types

/**
 * @brief PulseIsAborted
 * Retrieved together with certain callbacks, and allows to check if the callback has been marked for
 * abort/cancellation.
 * @param ctx The context configured with this callback.
 */
typedef bool (*PulseIsAborted) (void * ctx);

/**
 * @brief PulseAsyncOperationResultCallback
 * Retrieved as a callback for one on the async functions. Returns the result of the operation.
 * @param err The final error code for the processing of the async function.
 * @param user_context The user context configured with this callback.
 */
typedef void (*PulseAsyncOperationResultCallback) (const PulseError err, void * user_context);

/**
 * @brief PulseAppChannelKind
 * Identifies which wire a packet flowed on (or should flow on) when the
 * application owns the transport. Today bundle mode always uses
 * #PULSE_APP_CHANNEL_KIND_MUX (a single wire carrying both RTP and
 * RTCP). SIP non-bundle (with `a=rtcp-mux` off) splits each `m=`
 * section's transport into two wires: one #PULSE_APP_CHANNEL_KIND_RTP
 * and one #PULSE_APP_CHANNEL_KIND_RTCP.
 */
typedef enum
{
  PULSE_APP_CHANNEL_KIND_MUX = 0, /**< RTP+RTCP muxed on a single wire (the default). */
  PULSE_APP_CHANNEL_KIND_RTP,     /**< RTP-only wire (rtcp-mux disabled on this `m=` section). */
  PULSE_APP_CHANNEL_KIND_RTCP,    /**< RTCP-only wire (rtcp-mux disabled on this `m=` section). */
} PulseAppChannelKind;

/**
 * @brief PulseAppChannelId
 * Identifies an application-driven transport wire end-to-end. Forwarded
 * with every outbound #PulseAppPacketCallback invocation and consumed
 * by every #pulse_app_transport_push call.
 *
 * **Bundle mode** (today's WebRTC/Infinity calls): exactly one wire
 * exists per call. The @ref content and @ref type fields are not
 * meaningful — applications should read only @ref kind (which is
 * always #PULSE_APP_CHANNEL_KIND_MUX). A zero-initialised
 * #PulseAppChannelId is the canonical bundle channel id.
 *
 * **SIP non-bundle mode**: each `m=` section gets one (mux) or two
 * (split RTP+RTCP) channels. @ref content + @ref type identify which
 * `m=` section, and @ref kind distinguishes the RTP wire from the
 * RTCP wire when rtcp-mux is off.
 */
typedef struct
{
  PulseMediaContent content; /**< Which `m=` section this wire belongs to (non-bundle only). */
  PulseMediaType type;       /**< Which `m=` section this wire belongs to (non-bundle only). */
  PulseAppChannelKind kind;  /**< MUX vs split RTP/RTCP wire. */
} PulseAppChannelId;

/**
 * @brief PulseAppPacketCallback
 * Callback invoked once per outbound packet that the client wants to deliver via
 * application-driven transport. Configured via pulse_options_set_app_transport().
 *
 * Threading: the callback may be invoked on any internal media/streaming thread. The callback
 * implementation MUST NOT call back into Pulse synchronously (e.g. it must not call
 * pulse_disconnect, pulse_free, pulse_options_set_app_transport, ...). It IS safe to call
 * pulse_app_transport_push() synchronously from this callback (e.g. for loopback testing or when the
 * application's transport is itself synchronous).
 *
 * Buffer borrow: the @p data buffer is borrowed and only valid for the duration of the call. The
 * application MUST copy the bytes if it needs to retain them past return.
 *
 * Backpressure: the callback returns void; outbound packets that cannot be sent are best-effort
 * dropped by the application. This matches the underlying media layer semantics.
 *
 * Channel id semantics: @p channel_id identifies the wire the packet flowed on. In bundle mode
 * the application should ignore @p channel_id.content and @p channel_id.type and look only at
 * @p channel_id.kind (always #PULSE_APP_CHANNEL_KIND_MUX). In SIP non-bundle mode all three
 * fields together identify the per-`m=` per-kind wire — see #PulseAppChannelId.
 *
 * @param user_context The user context registered with the callback.
 * @param channel_id   Identifies the wire this packet flowed on.
 * @param data Borrowed pointer to a packet (valid only for the duration of the call). In bundle
 *             mode this is a muxed RTP/RTCP packet; in split mode the @p channel_id.kind tells
 *             you whether this is an RTP or RTCP packet.
 * @param size Length of @p data in bytes.
 */
typedef void (*PulseAppPacketCallback) (void * user_context, PulseAppChannelId channel_id, const uint8_t * data,
                                        int size);

/**
 * @brief PulseDestroyCallback
 * Generic destroy callback used to release application-owned resources tied to a Pulse binding.
 * Invoked synchronously from the function that releases the binding (either an explicit
 * unbind/rebind via pulse_options_set_app_transport(), or implicitly during pulse_free()), after
 * any in-flight callback invocations have drained. After this callback returns, Pulse holds no
 * further references to @p user_context.
 *
 * @param user_context The user context originally associated with the binding.
 */
typedef void (*PulseDestroyCallback) (void * user_context);

/**
 * @brief PulseOperationProgressCallback
 * Callback function to get progress updates from async and sync functions that implement it.
 * @param progress_info A PulseOperationProgressInfo struct, containing progress information.
 * @param user_context The user context configured with this callback.
 */
typedef void (*PulseOperationProgressCallback) (const PulseOperationProgressInfo * progress_info, void * user_context);

/**
 * @brief PulseLoggingCallback
 * Callback function used as log handler for all logging.
 * @param user_context the user context configured with this callback.
 * @param level the log level for the emitted message.
 * @param category the category to log the message at.
 * @param wall_time_us the time at which the log message ocurred.
 * @param elapsed_nano the elapsed time since the instance of Pulse started.
 * @param pid the process ID of the underlying media instance (PMX).
 * @param file the file that emitted the message.
 * @param function the function that emitted the message.
 * @param line the line in the file from which the message was emitted.
 * @param object_debug_str string representation of the object (if any) that emitted the message.
 * @param message the emitted message.
 };
 */
typedef void (*PulseLoggingCallback) (void * user_context, PulseDebugLevel level, const char * category,
                                      int64_t wall_time_us, int64_t elapsed_nano, unsigned int pid, const char * file,
                                      const char * function, int line, const char * object_debug_str,
                                      const char * message);

/**
 * @brief PulseVersionCallback
 * Callback function to inform that the attempted connection is done to a server of unsupported version.
 * @param server_major_version The major version of the Infinity server we're attempting to connect to.
 * @param server_minor_version The minor version of the Infinity server we're attempting to connect to.
 * @param user_context The user context configured with this callback.
 * @return This callback should return TRUE(1) if we accept the connection even though its unsupported, or FALSE(1) if
 * we want to stop this connection.
 */
typedef bool (*PulseVersionCallback) (uint64_t server_major_version, uint64_t server_minor_version,
                                      void * user_context);

/**
 * @brief PulseConferenceStatusInfoCallback
 * Callback function to get updates about the state of a connection.
 * @param status_info A PulseConferenceStatusInfo struct, containing information about the status of the conference.
 * @param user_context The user context configured with this callback.
 */
typedef void (*PulseConferenceStatusInfoCallback) (const PulseConferenceStatusInfo * status_info, void * user_context);

/**
 * @brief PulseRegistrationStatusInfoCallback
 * Callback function to get updates about the state of a connection.
 * @param status_info A PulseRegistrationStatusInfo struct, containing information about the status of the registration.
 * @param user_context The user context configured with this callback.
 */
typedef void (*PulseRegistrationStatusInfoCallback) (const PulseRegistrationStatusInfo * status_info,
                                                     void * user_context);

/**
 * @brief PulseNetworkStatusInfoCallback
 * Callback function to get updates about the network state.
 * @param network_status_info A PulseNetworkStatusInfo struct, containing information about the network availabillity
 * and limitations.
 * @param user_context The user context configured with this callback.
 */
typedef void (*PulseNetworkStatusInfoCallback) (const PulseNetworkStatusInfo * network_status_info,
                                                void * user_context);

/**
 * @brief PulsePinCodeRequestCallback
 * Callback functions for querying a client for a pin code.
 * @param guest_pin_required True if a pin code is required for joining as a guest, False otherwise.
 * @param pin_set_pin_code A PulseSetPinCode struct containing a function pointer for setting the pin code.
 * @param user_context The user context that was configured for this callback.
 * @note The included PulseSetPinCode function must be called prior to returning back from this callback.
 */
typedef bool (*PulsePinCodeRequestCallback) (bool guest_pin_required, const PulseSetPinCode * pin_set_pin_code,
                                             void * user_context);

/**
 * @brief PulseSetPinCodeFunc
 * Retrieved as part of the call to PulsePinCodeRequestCallback. Registers a new pin_code to a running session during
 * setup.
 * @param context The context retrieved as part of the PulsePinCodeRequest struct.
 * @param pin_code The new pin code to register to the session. Pin_code can be NULL when a guest pin is not required.
 * @note This function can ONLY be called from within a PulsePinCodeRequestCallback function, and is invalid in all
 * other contexts.
 */
typedef bool (*PulseSetPinCodeFunc) (void * context, const char * pin_code);

/**
 * @brief PulseConferenceExtensionRequestCallback
 * Callback functions for querying a client for a conference extentsion code when calling a IVR (virtual reception).
 * @param set_conference_extension A PulseSetConferenceExtension struct containing a function information for setting
 * the conference extension.
 * @param user_context The user context that was configured for this callback.
 * @note The function included in the PulseSetConferenceExtension struct must be called prior to returning back from
 * this callback.
 */
typedef bool (*PulseConferenceExtensionRequestCallback) (const PulseSetConferenceExtension * set_conference_extension,
                                                         void * user_context);

/**
 * @brief PulseSetConferenceExtensionFunc
 * Retrieved as part of the call to PulseConferenceExtensionRequestCallback. Registers a new conference extension with a
 * running session during setup.
 * @param context The context retrieved as part of the PulsePinCodeRequest struct.
 * @param conference_extension The new conference extension to register to the session. Conference extension should be a
 * digits only.
 * @note This function can ONLY be called from within a PulseConferenceExtensionRequestCallback function, and is invalid
 * in all other contexts.
 */
typedef bool (*PulseSetConferenceExtensionFunc) (void * context, const char * conference_extension);

/**
 * @brief PulseUpdateSDPCallback
 * @param context The context retrieved as part of the PulseExternalRestCallbackConfig struct.
 * @param update_sdp The new updated SDP.
 * @note This function will be called once Pulse needs to inform the application about changes in its SDP.
 */
typedef void (*PulseUpdateSDPCallback) (void * context, const char * update_sdp);

/**
 * @brief PulseSSOProviderSelectionCallback
 * Callback functions for allowing client side selection of SSO provider.
 * @param list A PulseSSOProviderList list of possible SSO candidates.
 * @param user_context The user context configured with this callback.
 */
typedef int (*PulseSSOProviderSelectionCallback) (PulseSSOProviderList * list, void * user_context);

/**
 * @brief PulseSSOProviderSetTokenFunc
 * Retrieved as part of the call to PulseSSOProviderRequestCallback. Registers a new sso-token to a running session
 * during setup.
 * @param context The context retrieved as part of the PulseSSOProviderSetToken struct.
 * @param sso_token The new token to register to the session.
 * @note This function can ONLY be called from within a PulseSSOProviderRequestCallback function, and is invalid in all
 * other contexts.
 */
typedef bool (*PulseSSOProviderSetTokenFunc) (void * context, const char * sso_token);

/**
 * @brief PulseSSOProviderRequestCallback
 * Callback function to perform the SSO Provider Request call, parse the response, and return the sso-token via the
 * provided PulseSSOProviderSetToken function.
 * @param request A PulseSSOProviderRequest structure containing the information needed to perform an SSO
 * authentication.
 * @param set_sso_token A PulseSSOProviderSetToken structure containing a function pointer and context, used for storing
 * a new sso-token.
 * @param user_context The user context configured with this callback.
 * @note The included PulseSSOProviderSetToken function must be called prior to returning from this callback.
 */
typedef bool (*PulseSSOProviderRequestCallback) (PulseSSOProviderRequest * request,
                                                 PulseSSOProviderSetToken * set_sso_token, void * user_context);

/**
 * @brief PulsetorageSetDataCallback
 *
 * @param key A string containing the key the data should be stored under.
 * @param value A string containing the data to be stored under the key. If value is NULL, this indicates a deletion.
 * @param user_context The user context configured with this callback.
 * @return 0 on success and -1 on error. Deletion of a non-existing key should return success.
 * @note If an error occurs, it is the responsibility of the callback to ensure that no partial data is persisted.
 */
typedef int (*PulseStorageSetDataCallback) (const char * key, const char * value, void * user_context);

/**
 * @brief PulseStorageGetDataCallback
 * Callback function to perform the SSO Provider Request call, parse the response, and return the sso-token via the
 * provided PulseSSOProviderSetToken function.
 * @param key A string containing the key for the data we wish to retrieve.
 * @param buf A pulse allocated character buffer that is at least buf_len long. Buf can be NULL, see note below!
 * @param buf_len The length of the provided buffer.
 * @param user_context The user context configured with this callback.
 * @return -1: buf_len too small, 0: entry not found, > 0 number of bytes returned.
 * @note If buf == NULL, this call is acting as a poll - don't attempt to copy any data, but report back the data_len
 * that would have been written, if the buffer was sufficiently large.
 */
typedef int (*PulseStorageGetDataCallback) (const char * key, char * buf, int buf_len, void * user_context);

/**
 * @brief PulseTLSDegradeApprovalCallback
 * Callback function to acknowledge TLS degrade when connecting to eg. a server that uses a certificate issued by an
 * unknown CA or a certificate that is expired.
 * @param reason A string describing the reason for the TLS degrade.
 * @param user_context The user context configured with this callback.
 */
typedef bool (*PulseTLSDegradeApprovalCallback) (const char * host, const char * reason, void * user_context);

/**
 * @brief PulseConferenceEventRemoteDisconnectCallback
 * Callback function to inform that our client is being disconnected from the Pexip side.
 * @param reason A string describing the reason for the disconnect.
 * @param user_context The user context configured with this callback.
 */
typedef void (*PulseConferenceEventRemoteDisconnectCallback) (const char * reason, void * user_context);

/**
 * @brief _PulseConferenceEventGenericCallback
 * Callback function to retrieve Rest server events.
 * @param event A PulseServerGenericEvent structure containing the event information.
 * @param user_context The user context configured with this callback.
 */
typedef void (*PulseConferenceEventGenericCallback) (PulseRoomId room_id, const PulseConferenceEventGeneric * event,
                                                     void * user_context);

/**
 * @brief PulseConferenceEventFeccCallback
 * Callback function to retrieve Rest server FECC events.
 * @param event A PulseConferenceEventFecc structure containing the event information.
 * @param user_context The user context configured with this callback.
 */
typedef void (*PulseConferenceEventFeccCallback) (PulseRoomId room_id, const PulseConferenceEventFecc * event,
                                                  void * user_context);

/**
 * @brief PulseConferenceEventMessageReceivedCallback
 * Callback function to retrieve chat messages broadcasted to the conference.
 * @param event A PulseConferenceEventMessageReceived structure containing the event information.
 * @param user_context The user context configured with this callback.
 */
typedef void (*PulseConferenceEventMessageReceivedCallback) (PulseRoomId room_id,
                                                             const PulseConferenceEventMessageReceived * event,
                                                             void * user_context);

/**
 * @brief PulseConferenceEventConferenceUpdateCallback
 * Callback function to retrieve an updated view of the conference state.
 * @param event A PulseConferenceEventConferenceUpdate structure containing the event information.
 * @param user_context The user context configured with this callback.
 */
typedef void (*PulseConferenceEventConferenceUpdateCallback) (PulseRoomId room_id,
                                                              const PulseConferenceEventConferenceUpdate * event,
                                                              void * user_context);

/**
 * @brief PulseConferenceEventParticipantListUpdatedCallback
 * Callback function to retrieve an updated view of the conference state.
 * @param list A PulseConferenceEventParticipantList structure containing the new participant list. It is the
 * responsibility of the callback function, to free the list, once its done with it.
 * @param user_context The user context configured with this callback.
 * @note A PulseConferenceEventParticipantList may contain 0 enties, which is needed for maintaining breakout rooms
 * roster lists for HOST participants. In this case its participant_list member will be NULL, so checking
 * participant_list_size is mandatory.
 * @note The returned list must be freed by calling pulse_conference_control_free_participant_list.
 */
typedef void (*PulseConferenceEventParticipantListUpdatedCallback) (PulseRoomId room_id,
                                                                    PulseConferenceEventParticipantList * list,
                                                                    void * user_context);

/**
 * @brief PulseConferenceEventParticipantCreateCallback
 * Callback function to retrieve information about a joining participant.
 * @param event A PulseConferenceEventParticipantCreate structure containing the event information.
 * @param user_context The user context configured with this callback.
 * @note This event is called after PulseConferenceEventParticipantListUpdatedCallback has completed, to make it
 * possible to extract participant information on client side..
 */
typedef void (*PulseConferenceEventParticipantCreateCallback) (PulseRoomId room_id,
                                                               const PulseConferenceEventParticipantCreate * event,
                                                               void * user_context);

/**
 * @brief PulseConferenceEventParticipantDeleteCallback
 * Callback function to retrieve information about a joining participant.
 * @param event A PulseConferenceEventParticipantDelete structure containing the event information.
 * @param user_context The user context configured with this callback.
 * @note This event is called before PulseConferenceEventParticipantListUpdatedCallback, to make it possible to extract
 * participant information on client side.
 */
typedef void (*PulseConferenceEventParticipantDeleteCallback) (PulseRoomId room_id,
                                                               const PulseConferenceEventParticipantDelete * event,
                                                               void * user_context);

/**
 * @brief PulseConferenceEventPresentationStartCallback
 * Callback function to inform about presentation starting.
 * @param event A PulseConferenceEventPresentationStart structure containing info about who is presentating.
 * @param user_context The user context configured with this callback.
 */
typedef void (*PulseConferenceEventPresentationStartCallback) (PulseRoomId room_id,
                                                               const PulseConferenceEventPresentationStart * event,
                                                               void * user_context);

/**
 * @brief PulseConferenceEventPresentationStartCallback
 * Callback function to inform about presentation stopping.
 * @param room_id The room the presentation belongs to.
 * @param user_context The user context configured with this callback.
 */
typedef void (*PulseConferenceEventPresentationStopCallback) (PulseRoomId room_id, void * user_context);

/**
 * @brief PulseConferenceEventLayoutCallback
 * Callback function to inform about layout changes.
 * @param room_id The room the layout change belongs to.
 * @param event A PulseConferenceEventLayout structure containing the updated layout info.
 * @param user_context The user context configured with this callback.
 */
typedef void (*PulseConferenceEventLayoutCallback) (PulseRoomId room_id, const PulseConferenceEventLayout * event,
                                                    void * user_context);

/**
 * @brief PulseConferenceEventStageCallback
 * Callback function to inform about stage changes.
 * @param room_id The room the stage change belongs to.
 * @param event A PulseConferenceEventStage structure containing the updated stage info.
 * @param user_context The user context configured with this callback.
 */
typedef void (*PulseConferenceEventStageCallback) (PulseRoomId room_id, const PulseConferenceEventStage * event,
                                                   void * user_context);

/**
 * @brief PulseConferenceEventLiveCaptionsCallback
 * Callback function to inform about live captions event.
 * @param room_id The room the live captions belong to.
 * @param event A PulseConferenceEventLiveCaptions structure containing the live captions event.
 * @param user_context The user context configured with this callback.
 */
typedef void (*PulseConferenceEventLiveCaptionsCallback) (PulseRoomId room_id,
                                                          const PulseConferenceEventLiveCaptions * event,
                                                          void * user_context);

/**
 * @brief PulseConferenceAudioMixerListCallback
 * Callback function to inform about updated audio mixers list.
 * @param room_id The room the mixer list belongs to.
 * @param list A PulseConferenceAudioMixersList structure containing the updated list of audio mixers.
 * @param user_context The user context configured with this callback.
 * @note The returned list must be freed by calling pulse_conference_control_free_audio_mixers_list.
 */
typedef void (*PulseConferenceAudioMixerListCallback) (PulseRoomId room_id, PulseConferenceAudioMixersList * list,
                                                       void * user_context);

/**
 * @brief PulseRegistrationsIncomingCallCallback
 * Callback function to signal an incoming call invitation.
 * @param event A PulseRegistrationsEventIncoming structure containing the event information.
 * @param user_context The user context configured with this callback.
 */
typedef bool (*PulseRegistrationsEventIncomingCallback) (
  const PulseRegistrationsEventIncoming * event, void * user_context,
  PulseAsyncOperationResultCallbackConfig * result_callback_config,
  PulseOperationProgressCallbackConfig * progress_callback_config);

/**
 * @brief PulseRegistrationsEventIncomingCancelledCallback
 * Callback function to signal that an incoming call invitation was cancelled.
 * @param event A PulseRegistrationsEventIncomingCancelled structure containing the event information.
 * @param user_context The user context configured with this callback.
 */
typedef void (*PulseRegistrationsEventIncomingCancelledCallback) (
  const PulseRegistrationsEventIncomingCancelled * event, void * user_context);

/**
 * @brief PulseAudioUnmuteApprovalCallback
 * Callback function to acknowledge server side unmute request.
 * @param user_context The user context configured with this callback.
 */
typedef bool (*PulseAudioUnmuteApprovalCallback) (void * user_context);

/**
 * @brief PulseAudioMuteStateChangedCallback
 * Callback function to inform about mute state changes.
 * @param client_mute_state Our local mute state, indicating if the local input is muted.
 * @param server_mute_state The server mute state, eg if we're muted remotely by a host.
 * @param user_context The user context configured with this callback.
 */
typedef void (*PulseAudioMuteStateChangedCallback) (bool client_mute_state, bool server_mute_state,
                                                    void * user_context);

/**
 * @brief PulseBreakoutRoomPreTransferCallback
 * Callback function to inform about an imminent breakoutroom transfer.
 * @param breakout_room_name The name of the room the client is about to be transferred to.
 * @note The transfer will not be performed until after this callback has returned, so this is a good place to
 * optionally wait a few seconds while displaying information about the transfer to the client.
 * @note There is no option to cancel the transfer.
 * @note This callback is emitted for both HOST and GUEST participants.
 */
typedef void (*PulseBreakoutRoomPreTransferCallback) (PulseRoomId room_id, const char * breakout_room_name,
                                                      void * user_context);

/**
 * @brief PulseBreakoutRoomPostTransferCallback
 * Callback function to inform that the client was transferred to a different breakout room.
 * @param room_id The id of the room the client was transferred to. For the main room, this is NULL.
 * @param user_context The user context configured with this callback.
 * @note This callback is emitted for both HOST and GUEST participants.
 */
typedef void (*PulseBreakoutRoomPostTransferCallback) (PulseRoomId room_id, void * user_context);

/**
 * @brief PulseBreakoutRoomTransferCancelledCallback
 * Callback function to inform that the last PulseBreakoutRoomPreTransferCallback was cancelled.
 * @param room_id The id of the room the client would have been transferred to. For the main room, this is NULL.
 * @param user_context The user context configured with this callback.
 * @note This callback is emitted for both HOST and GUEST participants.
 */
typedef void (*PulseBreakoutRoomTransferCancelledCallback) (PulseRoomId room_id, void * user_context);

/**
 * @brief PulseBreakoutRoomCreatedCallback
 * Callback function to inform that a new breakout room was created.
 * @param room_id The id of the newly created breakout room.
 * @param user_context The user context configured with this callback.
 * @note This callback is only emitted for HOST participants.
 * @note This callback can be called multiple times for the same uuid, if eg. a network change or error occured. In this
 * case, the state of the room (conference_stataus and participant lists) will be recreated completely.
 */
typedef void (*PulseBreakoutRoomCreatedCallback) (PulseRoomId room_id, void * user_context);

/**
 * @brief PulseBreakoutRoomDestroyedCallback
 * Callback function to inform that a existing breakout room was destroyed.
 * @param room_id The id of the breakout room that was destroyed.
 * @param user_context The user context configured with this callback.
 * @note This callback is only emitted for HOST participants.
 */
typedef void (*PulseBreakoutRoomDestroyedCallback) (PulseRoomId room_id, void * user_context);

/**
 * @brief PulseOperationProgressCallbackConfig
 * PulseOperationProgressCallbackConfig holds the config for a progress callback.
 */
struct _PulseOperationProgressCallbackConfig
{
  PulseOperationProgressCallback func;
  void * user_context;
};

/**
 * @brief PulseOperationProgressInfo
 * PulseOperationProgressInfo stores the progress info returned to a PulseOperationProgressCallback function.
 */
struct _PulseOperationProgressInfo
{
  const float progress; /*Range [0..1]*/
  const char * desc;    // Text describing the current status.
};

/**
 * @brief PulseVersionCallbackConfig
 * _PulseVersionCallbackConfig holds the config for the unsupported version callback.
 */
struct _PulseVersionCallbackConfig
{
  PulseVersionCallback func;
  void * user_context;
};

/**
 * @brief PulseConferenceStatusCallbackConfig
 * PulseConferenceStatusCallbackConfig holds the config for a conference status callback.
 */
struct _PulseConferenceStatusCallbackConfig
{
  PulseConferenceStatusInfoCallback func;
  void * user_context;
};

/**
 * @brief PulseConnectionStatusInfo
 * PulseConnectionStatusInfo holds information about the status of the current connection.
 */
struct _PulseConferenceStatusInfo
{
  PulseConnectionStatus status; // State of connection.
  bool is_blocked;              // While blocked, any attempt of calling connect/disconnect will fail.
  PulseConferenceServiceType current_service_type; // Only valid when is_connected == True
};

/**
 * @brief PulseConnectionStatusCallbackConfig
 * PulseConnectionStatusCallbackConfig holds the config for a connection status callback.
 */
struct _PulseRegistrationStatusCallbackConfig
{
  PulseRegistrationStatusInfoCallback func;
  void * user_context;
};

/**
 * @brief PulseConnectionStatusInfo
 * PulseConnectionStatusInfo holds information about the status of the current connection.
 */
struct _PulseRegistrationStatusInfo
{
  PulseConnectionStatus status; // State of connection.
  uint64_t next_reconnect_secs; /* Number of seconds until next reconnect attempt. Only valid when status ==
                                   PULSE_CONNECTION_STATUS_RECONNECTING */
};

/**
 * @brief PulseNetworkStatusCallbackConfig
 * PulseNetworkStatusCallbackConfig holds the config for a network status callback.
 */
struct _PulseNetworkStatusCallbackConfig
{
  PulseNetworkStatusInfoCallback func;
  void * user_context;
};

/**
 * @brief PulseNetworkStatusInfo
 * PulseNetworkStatusInfo holds information about the status of the network connection and availablilty.
 */
struct _PulseNetworkStatusInfo
{
  PulseNetworkConnectivityLevel connectivity;
  const char * connectivity_desc;
  bool is_metered;

  bool routing_probes_enabled;
  bool routing_alive;

  bool dns_probes_enabled;
  bool dns_alive;

  bool has_ipv4_address;
  char ipv4_address[16]; /* ASCII representation of the IPv4 address, or 0 if unavailable */

  bool has_ipv6_address;
  char ipv6_address[64]; /* ASCII representation of the IPv6 address, or 0 if unavailable */
};

/**
 * @brief PulseSetPinCode
 */
struct _PulseSetPinCode
{
  PulseSetPinCodeFunc func; // Funtion to call to to set the pin code
  void * context;           // Pulse context to send with PulseSetPinCodeFunc
};

/**
 * @brief PulsePinCodeRequestCallbackConfig
 * PulsePinCodeRequestCallbackConfig holds the config for setting up a PulsePinCodeRequestCallback callback.
 */
struct _PulsePinCodeRequestCallbackConfig
{
  PulsePinCodeRequestCallback func;
  void * user_context;
};

/**
 * @brief PulseSetConferenceExtension
 */
struct _PulseSetConferenceExtension
{
  PulseSetConferenceExtensionFunc func; // Funtion to call to to set the conference extension
  void * context;                       // Pulse context to send with PulseSetPinCodeFunc
};

/**
 * @brief PulseConferenceExtensionRequestCallbackConfig
 * PulsePinCodeRequestCallbackConfig holds the config for setting up a PulsePinCodeRequestCallback callback.
 */
struct _PulseConferenceExtensionRequestCallbackConfig
{
  PulseConferenceExtensionRequestCallback func;
  void * user_context;
};

/**
 * @brief PulseExternalRestCallbackConfig
 */
struct _PulseExternalRestCallbackConfig
{
  PulseUpdateSDPCallback update_sdp_callback;
  void * update_sdp_user_context;
};

/**
 * @brief PulseSSOProvider
 * PulseSSOProvider holds the information about a PulseSSOProviderList list member.
 */
struct _PulseSSOProvider
{
  const char * name;
  const char * uuid;
};

/**
 * @brief _PulseSSOProviderList
 * _PulseSSOProviderList holds the list of allowed SSO providers.
 */
struct _PulseSSOProviderList
{
  int num;
  PulseSSOProvider * providers;

  /* Some additional meta info about this request */
  PulseSSORequestType request_type;
  bool is_reconnect;
};

struct _PulseSSOProviderRequest
{
  PulseSSOProvider provider;
  const char * url;
  PulseIsAborted is_aborted_func;
  void * is_aborted_ctx;

  /* Some additional meta info about this request */
  PulseSSORequestType request_type;
  bool is_reconnect;
};

struct _PulseSSOProviderCallbackConfig
{
  PulseSSOProviderSelectionCallback selection_callback;
  void * selection_callback_user_context;
  PulseSSOProviderRequestCallback request_callback;
  void * request_callback_user_context;
};

struct _PulseStorageCallbackConfig
{
  PulseStorageSetDataCallback set_data_callback;
  void * set_data_callback_user_context;
  PulseStorageGetDataCallback get_data_callback;
  void * get_data_callback_user_context;
};

struct _PulseSSOProviderSetToken
{
  PulseSSOProviderSetTokenFunc func;
  void * context;
};

struct _PulseTLSDegradeApprovalCallbackConfig
{
  PulseTLSDegradeApprovalCallback func;
  void * user_context;
};

struct _PulseConferenceEventGeneric
{
  PulseConferenceEventType type;
  const char * event_name;
  const char * id;
  const char * data;
};

struct _PulseConferenceEventLayoutCoordinates
{
  uint64_t x;
  uint64_t y;
};

struct _PulseConferenceEventLayoutPresSlotCoords
{
  PulseConferenceEventLayoutCoordinates top_left;
  PulseConferenceEventLayoutCoordinates top_right;
  PulseConferenceEventLayoutCoordinates bottom_right;
  PulseConferenceEventLayoutCoordinates bottom_left;
};

struct _PulseConferenceEventLayout
{
  char * layout;
  uint32_t participants_num;
  const char ** participants;
  struct
  {
    struct
    {
      char * chair;
      char * guest;
    } primary_screen;
  } requested_layout;
  bool overlay_text_enabled;
  bool supports_pres_in_mix;
  PulseConferenceEventLayoutPresSlotCoords * pres_slot_coords;
};

struct _PulseConferenceEventStageSpeaker
{
  uint32_t stage_index;
  const char * participant_uuid;
  uint8_t vad;
};

struct _PulseConferenceEventStage
{
  uint32_t speakers_num;
  const PulseConferenceEventStageSpeaker ** speakers;
};

struct _PulseConferenceEventLiveCaptions
{
  char * data;
  bool is_final;
};

struct _PulseConferenceEventFecc
{
  PulseFeccActionType action;
  PulseFeccMovementDirection movement_direction_mask; /*One or more PulseFeccMovementDirection or'ed toghether.*/
  uint32_t timeout_ms;
};

struct _PulseConferenceEventMessageReceived
{
  const char * origin;
  const char * type;
  const char * payload;
  const char * uuid;
  bool direct;
};

struct _PulseConferenceEventPresentationStart
{
  const char * presenter_name;
  const char * presenter_uri;
  const char * presenter_uuid;
};

struct _PulseConferenceEventParticipantList
{
  size_t participant_list_size;
  struct _PulseConferenceControlParticipantEntry ** participant_list;
  uint32_t serial;
  bool is_search_result;
};

struct _PulseConferenceAudioMixersList
{
  size_t audio_mixers_list_size;
  char ** audio_mixers_list;
};

struct _PulseConferenceEventParticipantCreate
{
  char * uuid;
  bool is_state_sync;
};

struct _PulseConferenceEventParticipantUpdate
{
  char * uuid;
};

struct _PulseConferenceEventParticipantDelete
{
  char * uuid;
};

struct _PulseRegistrationsEventIncoming
{
  uint64_t timestamp; // The time when the event was received, given in number of ms since the Epoch, 1970-01-01
                      // 00:00:00 +0000 (UTC).
  uint32_t incoming_call_id;
  bool cancelled;
  char * conference_alias;
  char * local_alias;
  char * remote_alias;
  char * remote_display_name;
  PulseRegistrationIncomingCallSourceType source_type;
};

struct _PulseRegistrationsEventIncomingCancelled
{
  uint64_t timestamp; // The time when the event was received, given in number of ms since the Epoch, 1970-01-01
                      // 00:00:00 +0000 (UTC).
  uint32_t incoming_call_id;
  char * conference_alias;
  char * local_alias;
  char * remote_alias;
  char * remote_display_name;
  PulseRegistrationIncomingCallSourceType source_type;
};

struct _PulseRegistrationsEventCallbackConfig
{
  PulseRegistrationsEventIncomingCallback registrations_event_incoming_callback;
  void * registrations_event_incoming_callback_user_context;

  PulseRegistrationsEventIncomingCancelledCallback registrations_event_incoming_cancelled_callback;
  void * registrations_event_incoming_cancelled_callback_user_context;
};

struct _PulseAsyncOperationResultCallbackConfig
{
  PulseAsyncOperationResultCallback func;
  void * user_context;
};

struct _PulseRestConnectionConfig
{
  const char * server_address;
  const char * display_name;
  const char * conference_name;
  const char * pin_code;
};

struct _PulseTurnConfig
{
  const char * url;
  const char * username;
  const char * credential;
};

struct _PulseStunConfig
{
  const char * url;
};

struct _PulseSetupStage1Config
{
  bool is_sip; // If true, make a SDP suitable for SIP exchange, else create a webrtc SDP.
  bool disable_trickle_ice;
  PulseStunConfig * stun_config;
  PulseTurnConfig * turn_config;
};

struct _PulseSetupStage2Config
{
  const char * call_uuid;
  const char * remote_sdp;
};

struct _PulseMediaTxStats
{
  float ts_s;
  uint64_t first_rtp_activity;
  uint64_t last_rtp_activity;
  float rtt_ms;

  uint64_t total_packets_sent;
  uint64_t total_packets_lost;
  float total_packets_lost_pct;
  uint64_t total_bytes;
  uint32_t total_bitrate;
  float total_jitter_ms;

  float window_size;
  uint64_t windowed_packets_sent;
  uint64_t windowed_packets_lost;
  float windowed_packets_lost_pct;
  uint64_t windowed_bytes;
  uint32_t windowed_bitrate;
  float windowed_jitter_ms;

  uint32_t instant_bitrate;

  char * encoding_name;

  /* Video only stats below */
  uint32_t width;
  uint32_t height;
  double framerate;
};

struct _PulseMediaRxStats
{
  float ts_s;
  uint64_t first_rtp_activity;
  uint64_t last_rtp_activity;

  uint64_t total_packets_received;
  uint64_t total_packets_pushed;
  uint64_t total_packets_lost;
  float total_packets_lost_pct;

  uint64_t total_packets_actual_lost;
  float total_packets_actual_lost_pct;

  uint64_t total_packets_late;
  uint64_t total_packets_duplicates;
  uint64_t total_rtx_received;
  uint64_t total_rtx_success;
  float total_rtx_rtt_ms;
  uint64_t total_bytes;
  uint32_t total_bitrate;
  float total_jitter_ms;

  float window_size;
  uint64_t windowed_packets_received;
  uint64_t windowed_packets_pushed;
  uint64_t windowed_packets_lost;
  float windowed_packets_lost_pct;

  uint64_t windowed_packets_actual_lost;
  float windowed_packets_actual_lost_pct;

  uint64_t windowed_packets_late;
  uint64_t windowed_packets_duplicates;
  uint64_t windowed_rtx_received;
  uint64_t windowed_rtx_success;
  float windowed_rtx_rtt_ms;
  uint64_t windowed_bytes;
  uint32_t windowed_bitrate;
  float windowed_jitter_ms;

  uint32_t instant_bitrate;

  float jb_jitter_ms;
  uint32_t jb_latency_ms;

  char * encoding_name;

  /* Video only stats below */
  uint32_t width;
  uint32_t height;
  double framerate;

  bool face_detection_enabled;
  uint32_t no_face_duration_ms;
  uint32_t non_movement_duration_ms;
  int32_t num_faces;
};

struct _PulseMediaStats
{
  PulseMediaRxStats audio_rx;
  PulseMediaTxStats audio_tx;
  PulseMediaRxStats video_rx;
  PulseMediaTxStats video_tx;
  PulseMediaRxStats slides_rx;
  PulseMediaTxStats slides_tx;
};

struct _PulseMediaDebugData
{
  void * PmxRoot;
  void * PmxParticipant;
  void * PmxRtpSession;
  void * PmxVideoDeviceSessions[__PULSE_MEDIA_CONTENT_MAX];
  void * PmxAudioDeviceSessions[__PULSE_MEDIA_CONTENT_MAX];

  /* we support multiple audio inputs but for that use PmxAudioMixingGroup */
  unsigned int PmxLocalAudioOutputsIDs[__PULSE_MEDIA_CONTENT_MAX];

  /* we support multiple outputs, but for testing simplicity we set just the first one here */
  unsigned int PmxLocalVideoInputsIDs[__PULSE_MEDIA_CONTENT_MAX];
  unsigned int PmxLocalVideoOutputsIDs[__PULSE_MEDIA_CONTENT_MAX];

  unsigned int PmxInputIDAudioIn;
  unsigned int PmxInputIDVideoIn;
  unsigned int PmxOutputIDAudioOut;
  unsigned int PmxOutputIDVideoOut;
  unsigned int PmxInputIDAudioPresentation;
  unsigned int PmxInputIDVideoPresentation;
  unsigned int PmxOutputIDAudioPresentation;
  unsigned int PmxOutputIDVideoPresentation;
  int PmxAudioMixingGroup;
};

struct _PulseInfo
{
  const char * client_type;
  const char * session_state;

  struct PulseFipsInfo
  {
    bool enabled;
    bool configured;
    bool fips_so_mac_match;
    bool verified;
    const char * pex_base_path;
    const char * fips_so_path;
  } fips;

  struct PulseCurlInfo
  {
    const char * version;
    const char * ssl_version;
  } curl;
};

struct _PulseSessionInfo
{
  char * conference_name;
  char * conference_alias;
  char * conference_server;
  char * breakout_name;
  char * breakout_description;
};

typedef enum
{
  PULSE_HTTP_VERSION_2_0, /* Default behavior */
  PULSE_HTTP_VERSION_1_1,
  PULSE_HTTP_VERSION_1_0,
} PulseHttpVersion;

/**
 * @brief PulseProxyServerType
 * PulseProxyServerType defines a list of supported proxy server types.
 * Note: For a text representation of these types, check out pulse_type_mapping_proxy_server_type_to_string();
 */
typedef enum
{
  PULSE_PROXY_SERVER_HTTP,
  PULSE_PROXY_SERVER_HTTP_1_0,
  PULSE_PROXY_SERVER_HTTPS,
  PULSE_PROXY_SERVER_SOCKS4,
  PULSE_PROXY_SERVER_SOCKS4A,
  PULSE_PROXY_SERVER_SOCKS5,
  PULSE_PROXY_SERVER_SOCKS5H,
} PulseProxyServerType;

/**
 * @brief PulseProxyServerConfig
 * PulseProxyServerConfig holds the config for a proxy server connection.
 */
struct _PulseProxyServerConfig
{
  const char * proxy_server;       /* The hostname or IP address of the proxy server. Giving a URL is an error. */
  uint16_t proxy_port;             /* The port number */
  PulseProxyServerType proxy_type; /* The type of proxy server to connect to */

  /* The following members are only valid for
   * PULSE_PROXY_SERVER_HTTP|PULSE_PROXY_SERVER_HTTP_1_0|PULSE_PROXY_SERVER_HTTPS proxies */
  const char * proxy_username; /* If proxy server authentication is required, specify the username here */
  const char * proxy_password; /* If proxy server authentication is required, specify the password here */
};

typedef enum _PulseMessageContentType
{
  PULSE_MESSAGE_CONTENT_TYPE_PLAIN = 0 /* A plain text message. */
} PulseMessageContentType;

struct _PulseMessageRequest
{
  PulseMessageContentType content_type; /* The content type of the message buffer */
  const char * payload;                 /* The message payload to send. */
};

typedef enum
{
  PULSE_IVR_DISABLED,
  PULSE_IVR_STANDARD,
  PULSE_IVR_MSSIP,
} PulseVirtualReceptionType;

typedef enum
{
  PULSE_DISPLAY = 0,
  PULSE_WINDOW,
} PulseDesktopVideoType;

typedef enum
{
  PULSE_PARTICIPANT_LIST_SORTING_BY_JOIN_TIME,
  PULSE_PARTICIPANT_LIST_SORTING_ALPHABETICALLY,
} PulseParticipantListSortingOrder;

struct _PulseParticipantListSortingConfig
{
  PulseParticipantListSortingOrder order; /* The sorting order */
  bool reverse_order; /* Reverse the current sorting order. Does not override self_on_top or waiting_room_on_top */
  bool self_on_top;   /* Show our own participant on top of the list. Takes precedence over waiting_room_on_top. */
  bool waiting_room_on_top; /* Show any waiting participants on top of the list, but always below own participant, if
                               self_on_top is set. */
};

struct _PulseTuneables
{
  int placeholder; /* Nothing here now, but keeping it for later. */
};

PULSE_DECL_END

#endif /* _PULSE_TYPES_H_ */
