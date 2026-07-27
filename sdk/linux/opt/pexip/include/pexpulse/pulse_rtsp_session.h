/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2026 Pexip AS
 */

#ifndef _PULSE_RTSP_SESSION_H_
#define _PULSE_RTSP_SESSION_H_

#include <stdint.h>

#include "pulse_error.h"

/*
  PULSE RTSP SESSION

  Phase 1: input side only — dial out to an RTSP source (typically an IP
  camera) and ingest its streams as PMX inputs.

  RTSP is a *content source*, not a placement. A successfully-connected
  session is identified by an opaque #PulseRtspSessionID that the caller
  can subsequently:

    - place into a video mix surface via
      pulse_video_mix_input_from_rtsp_session() — possibly into multiple
      surfaces, of multiple mixes, simultaneously;
    - bind to a Pulse media-content slot (MAIN, PRESENTATION, …) via
      pulse_rtsp_session_bind_to_content() so PMX wires the session's
      audio/video into that role;
    - leave un-placed and consume directly via the input ids returned
      by pulse_rtsp_session_get_input_ids().

  Multiple concurrent sessions are supported: the same client can dial
  N different cameras and place each one independently.
*/

/**
 * @brief Opaque handle for a connected RTSP input session. Allocated by
 * pulse_rtsp_session_connect_input() and released by
 * pulse_rtsp_session_disconnect_input(). The value 0
 * (#PULSE_RTSP_SESSION_ID_NONE) is reserved as "no session".
 */
typedef uint32_t PulseRtspSessionID;

#define PULSE_RTSP_SESSION_ID_NONE ((PulseRtspSessionID)0)

/**
 * @brief Lower-transport for RTSP RTP/RTCP delivery.
 */
typedef enum
{
  PULSE_RTSP_TRANSPORT_TCP = 0,
  PULSE_RTSP_TRANSPORT_UDP,
  PULSE_RTSP_TRANSPORT_UDP_MCAST,
  PULSE_RTSP_TRANSPORT_HTTP,
} PulseRtspTransport;

/**
 * @brief Result of an RTSP state change.
 */
typedef enum
{
  PULSE_RTSP_SESSION_STATE_OK = 0,
  PULSE_RTSP_SESSION_STATE_DISCONNECTED,
  PULSE_RTSP_SESSION_STATE_AUTH_FAILED,
  PULSE_RTSP_SESSION_STATE_NOT_FOUND,
  PULSE_RTSP_SESSION_STATE_TIMEOUT,
} PulseRtspSessionState;

/** @brief Callback invoked when an RTSP input stream is discovered.
 *
 * Fired from the streaming context once `rtspsrc` exposes a new dynamic
 * source pad. PMX has by this point allocated a #PulseInputID and wired the
 * pad into the corresponding mama input.
 *
 * @param session_id The RTSP session that produced this input.
 * @param media_type Audio or video.
 * @param encoding_name RTP encoding-name from the SDP (e.g. "H264", "H265",
 *   "OPUS"); may be NULL if not present.
 * @param user_context User context provided in the input config.
 */
typedef void (*PulseRtspSessionInputAddedCb) (PulseRtspSessionID session_id, PulseMediaType media_type,
                                              const char * encoding_name, void * user_context);

/** @brief Callback invoked when an established RTSP session disconnects. */
typedef void (*PulseRtspSessionDisconnectCb) (PulseRtspSessionID session_id, PulseRtspSessionState state,
                                              void * user_context);

/**
 * @brief RTSP authentication credentials.
 */
typedef struct _PulseRtspAuthConfig
{
  const char * username; /* auth username */
  const char * password; /* auth password */
} PulseRtspAuthConfig;

/**
 * @brief RTSP input session callback configuration.
 */
typedef struct _PulseRtspInputCallbackConfig
{
  PulseRtspSessionInputAddedCb input_added_cb;
  PulseRtspSessionDisconnectCb disconnect_cb;
  void * user_context;
} PulseRtspInputCallbackConfig;

/**
 * @brief RTSP input (camera-dial-out) session configuration.
 */
typedef struct _PulseRtspInputConfig
{
  const char * location;        /* Full RTSP URL, e.g. "rtsp://camera.local/stream1". */
  PulseRtspTransport transport; /* Lower-transport: TCP (default), UDP, ... */
  uint32_t latency_ms;          /* RTP jitterbuffer latency in milliseconds (0 = rtspsrc default). */

  PulseRtspInputCallbackConfig callbacks;

  PulseRtspAuthConfig * auth_config; /* Optional auth (NULL for anonymous). */
} PulseRtspInputConfig;

PULSE_DECL_BEGIN

/**
 * @brief pulse_rtsp_session_connect_input
 * Connects a new RTSP input session by dialing out to @c config->location.
 * Returns once the camera SDP has been negotiated and inputs have been
 * enumerated via the input-added callback (or with an error if the
 * dial-out failed).
 *
 * The connect call does not pre-assign the session to any Pulse media
 * content slot or mix surface — that's a separate placement step (see
 * pulse_rtsp_session_bind_to_content() and
 * pulse_video_mix_input_from_rtsp_session()). Multiple concurrent
 * sessions are supported; each gets its own #PulseRtspSessionID.
 *
 * @param client The Pulse handle.
 * @param config The RTSP input configuration (deep-copied internally).
 * @param[out] out_session_id On success, the newly-allocated session
 *   handle. Set to #PULSE_RTSP_SESSION_ID_NONE on failure. Must be
 *   non-NULL.
 * @return PULSE_SUCCESS on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_rtsp_session_connect_input (Pulse * client, PulseRtspInputConfig * config,
                                             PulseRtspSessionID * out_session_id);

/**
 * @brief pulse_rtsp_session_disconnect_input
 * Disconnects and frees the RTSP input session identified by
 * @c session_id. Any inputs the session published into Pulse's media
 * routing (via pulse_rtsp_session_bind_to_content()) are unregistered;
 * any video-mix-input handles that referenced the session become stale
 * and their lookups will resolve to "no input".
 *
 * @param client The Pulse handle.
 * @param session_id The session handle returned by
 *   pulse_rtsp_session_connect_input().
 * @return PULSE_SUCCESS on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_rtsp_session_disconnect_input (Pulse * client, PulseRtspSessionID session_id);

/**
 * @brief pulse_rtsp_session_bind_to_content
 * Publishes the session's audio and video inputs into Pulse's
 * media-content slot routing for @c media_content (e.g.
 * #PULSE_MEDIA_CONTENT_MAIN or #PULSE_MEDIA_CONTENT_PRESENTATION).
 * After this call, the session participates in PMX's MAIN/PRESENTATION
 * wiring exactly as a camera or RTMP source bound to the same slot
 * would. #PULSE_MEDIA_CONTENT_SELFVIEW is not supported.
 *
 * Binding is independent of (and may be combined with) using the
 * session in any number of video-mix surfaces — the RTSP frames are
 * fanned out to every consumer.
 *
 * @param client The Pulse handle.
 * @param session_id The session handle returned by
 *   pulse_rtsp_session_connect_input().
 * @param media_content Target Pulse media content slot.
 * @return PULSE_SUCCESS on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_rtsp_session_bind_to_content (Pulse * client, PulseRtspSessionID session_id,
                                               PulseMediaContent media_content);

/**
 * @brief Sentinel "no input" value returned by
 * pulse_rtsp_session_get_input_ids() when the session has no audio
 * or no video leg.
 */
#define PULSE_RTSP_INPUT_ID_NONE ((uint32_t)-1)

/**
 * @brief pulse_rtsp_session_get_input_ids
 * Returns the first audio and video input id published by the
 * session, or #PULSE_RTSP_INPUT_ID_NONE for a leg the camera does
 * not provide. Both out-parameters are optional (pass NULL to skip).
 * The ids are stable for the lifetime of the session — disconnecting
 * invalidates them.
 *
 * The values returned here are opaque @c uint32_t handles in the Pulse
 * domain; they happen to be the underlying PMX input ids but callers
 * must not depend on that — treat them as session-scoped opaque
 * identifiers, useful only for diagnostic comparison (e.g. asserting
 * that two sessions surfaced distinct video inputs).
 *
 * @param client The Pulse handle.
 * @param session_id The session handle returned by
 *   pulse_rtsp_session_connect_input().
 * @param[out] out_audio_input_id Optional: receives the first audio
 *   input id, or #PULSE_RTSP_INPUT_ID_NONE if the session has no audio.
 * @param[out] out_video_input_id Optional: receives the first video
 *   input id, or #PULSE_RTSP_INPUT_ID_NONE if the session has no video.
 * @return PULSE_SUCCESS on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_rtsp_session_get_input_ids (Pulse * client, PulseRtspSessionID session_id,
                                             uint32_t * out_audio_input_id, uint32_t * out_video_input_id);

/*
  PULSE RTSP STREAM ITERATOR

  After pulse_rtsp_session_connect_input() returns success the camera's
  SDP has been fully negotiated and every stream announced by the server
  has been turned into its own input. The stream iterator surfaces
  these as enumerable, named, individually-pickable handles — modelled
  intentionally on the device iterator (#PulseDeviceIterator) so the
  same UX that hosts a "pick a microphone" dropdown can host a "pick an
  RTSP video sub-stream" dropdown with no architectural difference.

  Lifetime: a #PulseRtspStreamID is only meaningful relative to its
  owning #PulseRtspSessionID. Both become invalid the moment the
  session is disconnected via pulse_rtsp_session_disconnect_input().
  Iterators are snapshots — they survive subsequent stream changes
  (none happen today; rtspsrc emits the full set up-front via
  no-more-pads) but the stream pointers they hand out remain valid only
  until pulse_rtsp_stream_iterator_free() is called.
*/

/**
 * @brief Opaque identifier of an individual stream within an RTSP
 * session (for example, the camera's "video-0" or "audio-0" track).
 * Allocated as a side-effect of pulse_rtsp_session_connect_input(), and
 * remains stable for the lifetime of the owning session. The value 0
 * (#PULSE_RTSP_STREAM_ID_NONE) is reserved as "no stream".
 */
typedef uint32_t PulseRtspStreamID;

#define PULSE_RTSP_STREAM_ID_NONE ((PulseRtspStreamID)0)

/**
 * @brief Opaque per-item handle returned by the stream iterator. The
 * pointer is owned by the iterator and is only valid until
 * pulse_rtsp_stream_iterator_free() is called.
 */
typedef struct _PulseRtspStream PulseRtspStream;

/**
 * @brief Opaque iterator over the streams of an RTSP session, scoped to
 * a single #PulseMediaType. Allocated with
 * pulse_rtsp_session_stream_iterator_new() and released with
 * pulse_rtsp_stream_iterator_free().
 */
typedef struct _PulseRtspStreamIterator PulseRtspStreamIterator;

/**
 * @brief Function pointer for pulse_rtsp_stream_iterator_foreach().
 */
typedef void (*PulseRtspStreamIteratorFunc) (const PulseRtspStream * stream, void * user_context);

PULSE_EXPORT
/**
 * @brief Create a new stream iterator scoped to one media type.
 *
 * Snapshots the streams of the specified media type currently published
 * by @c session_id. Because pulse_rtsp_session_connect_input() blocks
 * until the camera's SDP has been fully negotiated and every stream
 * exposed, the iterator returned here is guaranteed to contain every
 * stream the server announced (or to be empty for a media type the
 * server does not provide).
 *
 * @param client The Pulse handle.
 * @param session_id A session previously created with
 *   pulse_rtsp_session_connect_input().
 * @param media_type Filter — either #PULSE_MEDIA_AUDIO or
 *   #PULSE_MEDIA_VIDEO.
 * @param[out] out_iterator On success, the new iterator handle. Must
 *   be non-NULL. Released with pulse_rtsp_stream_iterator_free().
 * @return PULSE_SUCCESS on success, or a PulseError code on failure
 *   (notably #PULSE_ERROR_NOT_CONFIGURED if @c session_id is unknown).
 */
PulseError pulse_rtsp_session_stream_iterator_new (Pulse * client, PulseRtspSessionID session_id,
                                                   PulseMediaType media_type, PulseRtspStreamIterator ** out_iterator);

PULSE_EXPORT
/**
 * @brief Release a stream iterator previously created with
 * pulse_rtsp_session_stream_iterator_new(). All #PulseRtspStream
 * pointers handed out by this iterator become invalid after the call.
 */
void pulse_rtsp_stream_iterator_free (PulseRtspStreamIterator * iterator);

PULSE_EXPORT
/**
 * @brief Number of streams in the iterator.
 */
int pulse_rtsp_stream_iterator_item_count (PulseRtspStreamIterator * iterator);

PULSE_EXPORT
/**
 * @brief Reset the iteration state and return the first stream, or NULL
 * if the iterator is empty. Returned pointer is owned by the iterator.
 */
const PulseRtspStream * pulse_rtsp_stream_iterator_first (PulseRtspStreamIterator * iterator);

PULSE_EXPORT
/**
 * @brief Return the next stream in the iteration chain, or NULL when
 * exhausted. Returned pointer is owned by the iterator.
 */
const PulseRtspStream * pulse_rtsp_stream_iterator_next (PulseRtspStreamIterator * iterator);

PULSE_EXPORT
/**
 * @brief Invoke @c func once per stream in the iterator, in order.
 */
PulseError pulse_rtsp_stream_iterator_foreach (PulseRtspStreamIterator * iterator, PulseRtspStreamIteratorFunc func,
                                               void * user_context);

PULSE_EXPORT
/**
 * @brief Stable per-session opaque id for a stream. Pass to
 * pulse_video_mix_input_from_rtsp_stream() to bind exactly this stream
 * (rather than the session's first matching input) into a video mix.
 */
PulseRtspStreamID pulse_rtsp_stream_get_id (const PulseRtspStream * stream);

PULSE_EXPORT
/**
 * @brief Human-readable display label for the stream — currently
 * derived from the per-pad arrival order (e.g. "video-0", "audio-1").
 * Returned pointer is owned by the iterator and remains valid until
 * pulse_rtsp_stream_iterator_free() is called.
 */
const char * pulse_rtsp_stream_get_name (const PulseRtspStream * stream);

PULSE_EXPORT
/**
 * @brief #PulseMediaType of the stream (audio or video).
 */
PulseMediaType pulse_rtsp_stream_get_media_type (const PulseRtspStream * stream);

PULSE_EXPORT
/**
 * @brief RTP encoding-name of the stream (e.g. "H264", "H265", "OPUS"),
 * or NULL if the camera's SDP did not advertise one. Returned pointer
 * is owned by the iterator and remains valid until
 * pulse_rtsp_stream_iterator_free() is called.
 */
const char * pulse_rtsp_stream_get_codec (const PulseRtspStream * stream);

PULSE_DECL_END

#endif /* _PULSE_RTSP_SESSION_H_ */
