/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2026 Pexip AS
 * @author Tulio Beloqui
 */

#ifndef _PULSE_VIDEO_MIX_SESSION_H_
#define _PULSE_VIDEO_MIX_SESSION_H_

#include "pulse_error.h"
#include "pulse_media.h"

/**
 * @brief Opaque identifier for a video input source.
 *
 * Obtained from one of the Pulse input APIs:
 *   - pulse_video_mix_input_from_device()
 *   - pulse_video_mix_input_from_desktop()
 *   - pulse_video_mix_input_from_file()
 *   - pulse_video_mix_input_from_data_session()
 *   - pulse_video_mix_input_from_rtmp_session()
 *
 * The caller is responsible for the lifecycle of the underlying source.
 * The mix session borrows the input for the duration of the mix.
 */
typedef uint32_t PulseVideoMixInputID;

#define PULSE_VIDEO_MIX_INPUT_ID_NONE ((PulseVideoMixInputID)0)

typedef enum
{
  PULSE_VIDEO_PROCESS_TYPE_NONE = 0,
  PULSE_VIDEO_PROCESS_TYPE_SEGMENTATION = 1 << 1,
  PULSE_VIDEO_PROCESS_TYPE_BLUR = 1 << 2,
} PulseVideoProcessTypeMask;

/**
 * @brief Describes a single input slot in a video mix composition.
 *
 * This struct describes *where* and *how* an input appears in the
 * composition.  It does NOT describe the source — that is identified
 * solely by @c input_id, which the caller obtained from a
 * pulse_video_mix_input_from_*() function.
 *
 * Fields:
 *   - input_id       An input previously obtained via
 *                    pulse_video_mix_input_from_device() or similar.
 *   - layer          Layer index (0-based). Same layer = same grid.
 *   - width_ratio    Fraction of layer width  (0.0–1.0).
 *   - height_ratio   Fraction of layer height (0.0–1.0).
 *   - x_centrepoint  Horizontal anchor (0.0=left, 0.5=center, 1.0=right).
 *   - y_centrepoint  Vertical anchor   (0.0=top,  0.5=center, 1.0=bottom).
 *   - videoproc_mask Per-input video processing (segmentation, blur).
 */
typedef struct
{
  PulseVideoMixInputID input_id;

  /* Layout placement */
  int layer;            /* Layer index — inputs with the same value share a layer */
  double width_ratio;   /* Fraction of layer width  (0.0–1.0, 0.0 = full)        */
  double height_ratio;  /* Fraction of layer height (0.0–1.0, 0.0 = full)        */
  double x_centrepoint; /* Horizontal anchor (0.0=left, 0.5=center, 1.0=right)   */
  double y_centrepoint; /* Vertical anchor   (0.0=top,  0.5=center, 1.0=bottom)  */

  /* Video processing */
  PulseVideoProcessTypeMask videoproc_mask;
} PulseVideoMixInput;

/**
 * @brief PulseVideoMixConfig
 * Configuration for a video mix session.
 *
 * Fields:
 *   - num_inputs Number of elements in the @c inputs array.
 *   - inputs     Array of PulseVideoMixInput entries.
 */
typedef struct
{
  size_t num_inputs;
  PulseVideoMixInput * inputs;
} PulseVideoMixConfig;

PULSE_DECL_BEGIN

/* -----------------------------------------------------------------------
 * Mix session lifecycle
 * ----------------------------------------------------------------------- */

/**
 * @brief Connect (start) a video mix session.
 *
 * Sets up a compositing pipeline described by @c config and attaches it to
 * @c media_content.  Any previously connected video source on the same
 * @c media_content will result in PULSE_ERROR_UNEXPECTED_STATE, so disconnect must
 * be called first.
 *
 * @param client        The Pulse handle.
 * @param config        The video mix configuration describing all layers.
 * @param media_content The media content slot to attach the mix to
 *                      (e.g. PULSE_MEDIA_CONTENT_MAIN).
 * @return PULSE_SUCCESS (0) on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_video_mix_connect (Pulse * client, const PulseVideoMixConfig * config,
                                    PulseMediaContent media_content);

/**
 * @brief Disconnect (stop) a video mix session.
 *
 * Unregister from the media routing layer
 * Clear the session's own state (layout, input IDs array)
 * Is not responsible for releasing inputs.
 *
 * @param client        The Pulse handle.
 * @param media_content The media content slot to disconnect.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code on failure.
 */
PULSE_EXPORT
PulseError pulse_video_mix_disconnect (Pulse * client, PulseMediaContent media_content);

PULSE_DECL_END

#endif /* _PULSE_VIDEO_MIX_SESSION_H_ */