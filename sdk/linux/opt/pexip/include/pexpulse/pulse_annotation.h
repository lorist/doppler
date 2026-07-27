/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2026 Pexip AS
 */

#ifndef _PULSE_ANNOTATION_H_
#define _PULSE_ANNOTATION_H_

#include "pulse_error.h"
#include "pulse_media.h"
#include "pulse_types.h"

PULSE_DECL_BEGIN

/**
 * @brief Opaque handle for a "drawing surface" inside Pulse.
 *
 * An annotation handle identifies an in-memory canvas that the caller
 * paints on with the primitives below. Pulse owns the canonical list
 * of strokes ("layers") for each surface and is responsible for
 * rendering them into a video frame.
 *
 * How a handle is obtained depends on what the caller wants to do
 * with the resulting frames:
 *   - To overlay drawings on top of an existing video mix (camera /
 *     desktop / file), call pulse_video_mix_input_from_annotation()
 *     in pulse_video_mix_input.h. That returns a PulseVideoMixInputID
 *     which can be passed to all of the primitives below — the two
 *     handle types share the same numeric space.
 *   - Future: a "pulse_presentation_from_annotation()" or similar
 *     entry point will let the same canvas be sent as an outgoing
 *     presentation stream (whiteboard use case), with no mixer
 *     involved at all. The drawing API is intentionally decoupled
 *     from the video-mix API so the caller does not need to care.
 *
 * Coordinate space:
 *   All x/y values passed to pulse_annotation_stroke_add_point() are
 *   in canvas pixel coordinates, where (0, 0) is the top-left of the
 *   canvas and (width - 1, height - 1) is the bottom-right. The
 *   canvas dimensions are fixed at acquire time. Mapping pointer
 *   events from a display widget back into canvas pixels is the
 *   caller's responsibility.
 *
 * Thread-safety: every public entry point takes Pulse's internal
 * mutex.
 */
typedef uint32_t PulseAnnotationID;

#define PULSE_ANNOTATION_ID_NONE ((PulseAnnotationID)0)

/**
 * @brief Opaque identifier for an in-progress drawing stroke.
 *
 * Returned by pulse_annotation_stroke_begin(); used by subsequent
 * pulse_annotation_stroke_add_point() / _set_last_point() / _end()
 * calls to refer back to the stroke. Stroke IDs are scoped to a
 * single PulseAnnotationID and are not interchangeable between
 * different surfaces.
 */
typedef uint32_t PulseAnnotationStrokeID;

#define PULSE_ANNOTATION_STROKE_ID_NONE ((PulseAnnotationStrokeID)0)

/* -----------------------------------------------------------------------
 * Style state for the next stroke
 * ----------------------------------------------------------------------- */

/**
 * @brief Set the current stroke colour as RGBA components (0-255).
 */
PULSE_EXPORT
PulseError pulse_annotation_set_color (Pulse * client, PulseAnnotationID id, uint8_t r, uint8_t g, uint8_t b,
                                       uint8_t a);

/**
 * @brief Set the current stroke thickness in canvas pixels.
 *
 * A thickness of 0 means "no stroke"; the stroke would be rendered
 * empty. Callers should pass at least 1 to see anything.
 */
PULSE_EXPORT
PulseError pulse_annotation_set_thickness (Pulse * client, PulseAnnotationID id, uint32_t thickness_px);

/**
 * @brief Enable or disable a solid background fill on the canvas.
 *
 * By default an annotation surface is fully transparent — strokes
 * float on top of whatever is rendered behind in the mixer (camera,
 * desktop, slide). Toggling a background on turns the same surface
 * into a "whiteboard" / "blackboard" / "$colour-board" — the entire
 * canvas is filled with the supplied RGBA colour and strokes are
 * drawn over the fill.
 *
 * Implementation-wise the background is rendered as a fullscreen
 * filled rectangle at layer index 0 (below all strokes) so undo /
 * redo / clear continue to operate only on user strokes — wiping
 * the canvas does not also wipe the whiteboard.
 *
 * @param client   The Pulse handle.
 * @param id       The annotation surface to update.
 * @param enabled  TRUE to fill the canvas, FALSE to keep it transparent.
 * @param r        Background red component (0-255).
 * @param g        Background green component (0-255).
 * @param b        Background blue component (0-255).
 * @param a        Background alpha component (0-255). Ignored when @c enabled
 *                 is FALSE. An @c a of 0 with @c enabled TRUE is permitted but
 *                 renders the same as @c enabled FALSE.
 */
PULSE_EXPORT
PulseError pulse_annotation_set_background (Pulse * client, PulseAnnotationID id, bool enabled, uint8_t r, uint8_t g,
                                            uint8_t b, uint8_t a);

/* -----------------------------------------------------------------------
 * Stroke construction (a "layer" == one undoable stroke)
 * ----------------------------------------------------------------------- */

/**
 * @brief Begin a new stroke (one drawing layer).
 *
 * Snapshots the current colour + thickness. The returned stroke id
 * is used to add points to and to end the stroke. Each begin must be
 * matched by exactly one _stroke_end().
 *
 * @param client              The Pulse handle.
 * @param id                  The annotation surface to draw on.
 * @param[out] out_stroke_id  The resolved stroke identifier.
 */
PULSE_EXPORT
PulseError pulse_annotation_stroke_begin (Pulse * client, PulseAnnotationID id,
                                          PulseAnnotationStrokeID * out_stroke_id);

/**
 * @brief Append a point to an in-progress stroke.
 */
PULSE_EXPORT
PulseError pulse_annotation_stroke_add_point (Pulse * client, PulseAnnotationID id, PulseAnnotationStrokeID stroke_id,
                                              int32_t x, int32_t y);

/**
 * @brief Replace the last point of an in-progress stroke.
 *
 * Useful for "rubber-band" style tools (e.g. line tool) where the
 * end-point follows the cursor while the mouse button is held. If the
 * stroke currently has zero points, behaves like _add_point().
 */
PULSE_EXPORT
PulseError pulse_annotation_stroke_set_last_point (Pulse * client, PulseAnnotationID id,
                                                   PulseAnnotationStrokeID stroke_id, int32_t x, int32_t y);

/**
 * @brief Mark a stroke as complete.
 *
 * Triggers an immediate flush of the layer list to the rendering
 * backend. Strokes with zero points are silently dropped (they are
 * not pushed onto the undo stack).
 */
PULSE_EXPORT
PulseError pulse_annotation_stroke_end (Pulse * client, PulseAnnotationID id, PulseAnnotationStrokeID stroke_id);

/* -----------------------------------------------------------------------
 * Layer management
 * ----------------------------------------------------------------------- */

/**
 * @brief Pop the most recently completed stroke onto an internal redo
 *        stack and re-render. No-op if no completed strokes exist.
 */
PULSE_EXPORT
PulseError pulse_annotation_undo (Pulse * client, PulseAnnotationID id);

/**
 * @brief Push the most recently undone stroke back onto the layer list
 *        and re-render. No-op if the redo stack is empty.
 */
PULSE_EXPORT
PulseError pulse_annotation_redo (Pulse * client, PulseAnnotationID id);

/**
 * @brief Drop all layers (completed, in-progress, and the redo stack)
 *        and re-render an empty canvas.
 */
PULSE_EXPORT
PulseError pulse_annotation_clear (Pulse * client, PulseAnnotationID id);

/**
 * @brief Force a re-render of the current layout.
 *
 * pulse_annotation_stroke_add_point() and _set_last_point() do not by
 * themselves push the updated layout to the rendering backend (that
 * could cost one full pipeline state-change per pointer event during
 * a pencil drag). Callers that want to see in-progress strokes update
 * on screen should call _commit() at most once per UI frame while the
 * mouse is held.
 */
PULSE_EXPORT
PulseError pulse_annotation_commit (Pulse * client, PulseAnnotationID id);

PULSE_DECL_END

#endif /* _PULSE_ANNOTATION_H_ */
