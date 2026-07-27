/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2022 Pexip AS
 * @author Knut Saastad
 * @author Tulio Beloqui
 */

#ifndef _PULSE_RTMP_SESSION_H_
#define _PULSE_RTMP_SESSION_H_

#include "pulse_error.h"

/*
  PULSE RTMP SESSION
*/

/** @brief Callback invoked to accept or reject an incoming RTMP publish request. */
typedef bool (*PulseRtmpSessionPublishAcceptCb) (int client_id, const char * path, const char * params,
                                                 void * user_context);
/** @brief Callback invoked when an RTMP publish stream starts. */
typedef void (*PulseRtmpSessionPublishStartCb) (int client_id, const char * path, const char * params,
                                                void * user_context);
/** @brief Callback invoked when an RTMP publish stream stops. */
typedef void (*PulseRtmpSessionPublishStopCb) (int client_id, const char * path, const char * params,
                                               uint32_t server_status /*Wrap PexRtmpServerStatus*/,
                                               void * user_context);

/** @brief Callback invoked to accept or reject an incoming RTMP play request. */
typedef bool (*PulseRtmpSessionPlayAcceptCb) (int client_id, const char * path, const char * params,
                                              void * user_context);
/** @brief Callback invoked when an RTMP play stream starts. */
typedef void (*PulseRtmpSessionPlayStartCb) (int client_id, const char * path, const char * params,
                                             void * user_context);
/** @brief Callback invoked when an RTMP play stream stops. */
typedef void (*PulseRtmpSessionPlayStopCb) (int client_id, const char * path, const char * params,
                                            uint32_t server_status /*Wrap PexRtmpServerStatus*/, void * user_context);

/** @brief Callback invoked when an RTMP session disconnects. */
typedef void (*PulseRtmpSessionDisconnectCb) (PulseMediaContent content, PulseMediaDirection direction,
                                              const char * reason, void * user_context);

/**
 * @brief RTMP TLS configuration for input (server) sessions.
 * Specifies certificates and ciphers for an RTMPS listener.
 */
typedef struct _PulseRtmpTlsInputConfig
{
  const char * cert_file; /* File containing TLS certificate */
  const char * key_file;  /* File containing TLS private key */
  const char * ciphers;   /* Specification of ciphers to use */
} PulseRtmpTlsInputConfig;

/**
 * @brief RTMP TLS configuration for output (client) sessions.
 * Specifies trusted CA directory and ciphers for outbound RTMPS connections.
 */
typedef struct _PulseRtmpTlsOutputConfig
{
  const char * ca_cert_dir; /* Directory containing trusted CA certificates */
  const char * ciphers;     /* Specification of ciphers to use */
} PulseRtmpTlsOutputConfig;

/**
 * @brief RTMP authentication credentials.
 * Specifies the username and password for RTMP authentication.
 */
typedef struct _PulseRtmpAuthConfig
{
  const char * username; /* auth username */
  const char * password; /* auth password */
} PulseRtmpAuthConfig;

/**
 * @brief RTMP input session callback configuration.
 * Groups publish and play callbacks for an RTMP input (server) session.
 */
typedef struct _PulseRtmpInputCallbackConfig
{
  /* Publish callbacks*/
  PulseRtmpSessionPublishAcceptCb publish_accept_cb;
  PulseRtmpSessionPublishStartCb publish_start_cb;
  PulseRtmpSessionPublishStopCb publish_stop_cb;
  void * publish_uc;

  /* Play callbacks*/
  PulseRtmpSessionPlayAcceptCb play_accept_cb;
  PulseRtmpSessionPlayStartCb play_start_cb;
  PulseRtmpSessionPlayStopCb play_stop_cb;
  void * play_uc;
} PulseRtmpInputCallbackConfig;

/**
 * @brief RTMP output session callback configuration.
 * Groups disconnect callbacks for an RTMP output (client) session.
 */
typedef struct _PulseRtmpOutputCallbackConfig
{
  /* Disconnect handling */
  PulseRtmpSessionDisconnectCb disconnect_cb;
  void * disconnect_uc;
} PulseRtmpOutputCallbackConfig;

/**
 * @brief RTMP input (server) session configuration.
 * Specifies the listening parameters, media support, callbacks, and optional
 * TLS/auth configuration for an inbound RTMP session.
 */
typedef struct _PulseRtmpInputConfig
{
  const char * path;       /* The server path. This will be overridden by
                              PulseRtmpSessionPublishAcceptCb/PulseRtmpSessionPlayAcceptCb if configured */
  uint16_t listening_port; /* TCP port to listen to. IANA port 1935 */
  bool use_tls;            /* Use TLS (RTMPS) for the connection */
  bool support_audio;      /* Should we support audio over RTMP */
  bool support_video;      /* Should we support video over RTMP */

  PulseRtmpInputCallbackConfig callbacks;

  PulseRtmpAuthConfig * auth_config;
  PulseRtmpTlsInputConfig * tls_config;
} PulseRtmpInputConfig;

/**
 * @brief RTMP output (client) session configuration.
 * Specifies the target host, port, media options, callbacks, and optional
 * TLS/auth configuration for an outbound RTMP session.
 */
typedef struct _PulseRtmpOutputConfig
{
  const char * hostname; /* The hostname of the server. */
  uint16_t port;         /* TCP port we will bind to for sending. */
  bool use_tls;          /* Use TLS (RTMPS) for the connection */
  const char * path;     /* The server path. */

  uint16_t local_port;      /* Local TCP to bind to port we will bind to for sending, or 0. */
  const char * ip_override; /* Instead of resolving the hostname, attempt to connect with THIS ip address. */
  bool send_audio;          /* Should we send audio over RTMP */
  bool send_video;          /* Should we send video over RTMP */

  PulseRtmpOutputCallbackConfig callbacks;

  PulseRtmpAuthConfig * auth_config;
  PulseRtmpTlsOutputConfig * tls_config;
} PulseRtmpOutputConfig;

PULSE_DECL_BEGIN

/**
 * @brief pulse_rtmp_session_connect_input
 * Connects an RTMP input (server) session for the specified media content.
 * @param client The Pulse handle.
 * @param media_content The media content slot to use.
 * @param config The RTMP input configuration.
 * @return PULSE_SUCCESS on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_rtmp_session_connect_input (Pulse * client, PulseMediaContent media_content,
                                             PulseRtmpInputConfig * config);

/**
 * @brief pulse_rtmp_session_disconnect_input
 * Disconnects an RTMP input session for the specified media content.
 * @param client The Pulse handle.
 * @param media_content The media content slot to disconnect.
 * @return PULSE_SUCCESS on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_rtmp_session_disconnect_input (Pulse * client, PulseMediaContent media_content);

/**
 * @brief pulse_rtmp_session_connect_output
 * Connects an RTMP output (client) session for the specified media content.
 * @param client The Pulse handle.
 * @param media_content The media content slot to use.
 * @param config The RTMP output configuration.
 * @param connect_timeout_ms Connection timeout in milliseconds.
 * @return PULSE_SUCCESS on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_rtmp_session_connect_output (Pulse * client, PulseMediaContent media_content,
                                              PulseRtmpOutputConfig * config, uint32_t connect_timeout_ms);
/**
 * @brief pulse_rtmp_session_disconnect_output
 * Disconnects an RTMP output session for the specified media content.
 * @param client The Pulse handle.
 * @param media_content The media content slot to disconnect.
 * @return PULSE_SUCCESS on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_rtmp_session_disconnect_output (Pulse * client, PulseMediaContent media_content);

/**
 * @brief pulse_rtmp_session_wait_output_disconnect
 * Blocks until the RTMP output session for the specified media content disconnects.
 * @param client The Pulse handle.
 * @param media_content The media content slot to wait on.
 * @return PULSE_SUCCESS on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_rtmp_session_wait_output_disconnect (Pulse * client, PulseMediaContent media_content);

PULSE_DECL_END

#endif /* _PULSE_RTMP_SESSION_H_ */