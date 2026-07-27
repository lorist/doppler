package com.pexip.pulse.sample.screen.conference.annotation

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.unit.IntSize

/**
 * A single stroke drawn locally for immediate preview.
 * Points are stored in normalized coordinates (0..1) relative to the canvas bounds.
 */
internal data class LocalStroke(
    val points: List<Offset>,
    val color: Color,
    val thickness: Float
)

/**
 * Holds the local stroke state for [DrawingCanvas], allowing external callers
 * (e.g. undo/redo/clear buttons) to manipulate the local preview.
 */
class DrawingCanvasState {
    internal val completedStrokes = mutableStateListOf<LocalStroke>()
    internal val undoneStrokes = mutableStateListOf<LocalStroke>()

    /** Remove the last completed stroke (local undo). */
    fun undo() {
        if (completedStrokes.isNotEmpty()) {
            undoneStrokes.add(completedStrokes.removeAt(completedStrokes.lastIndex))
        }
    }

    /** Re-add the last undone stroke (local redo). */
    fun redo() {
        if (undoneStrokes.isNotEmpty()) {
            completedStrokes.add(undoneStrokes.removeAt(undoneStrokes.lastIndex))
        }
    }

    /** Clear all local strokes. */
    fun clear() {
        completedStrokes.clear()
        undoneStrokes.clear()
    }
}

@Composable
fun rememberDrawingCanvasState(): DrawingCanvasState {
    return remember { DrawingCanvasState() }
}

/**
 * Touch-based drawing canvas overlay with local preview.
 *
 * This composable must be sized and positioned to exactly match the video
 * surface it annotates (same aspect ratio, same alignment). Touch coordinates
 * are mapped 1:1 from this view's bounds to the annotation canvas dimensions.
 *
 * Local strokes are rendered immediately for visual feedback. Pulse renders
 * the same strokes into the outgoing video frame asynchronously.
 *
 * @param canvasWidth The annotation canvas width (in annotation coordinates).
 * @param canvasHeight The annotation canvas height (in annotation coordinates).
 * @param onStrokeBegin Called when a stroke begins with color (ARGB int), thickness (in canvas px), and start point.
 * @param onStrokeAddPoint Called when a point is added to the active stroke.
 * @param onStrokeEnd Called when the stroke ends.
 * @param strokeColor Current stroke color.
 * @param strokeThickness Current stroke thickness in display pixels.
 * @param enabled Whether touch drawing is enabled (paint mode active).
 * @param state The [DrawingCanvasState] for local stroke management.
 * @param modifier Modifier — caller must constrain this to match the video bounds.
 */
@Composable
fun DrawingCanvas(
    canvasWidth: Int,
    canvasHeight: Int,
    onStrokeBegin: (color: Int, thickness: Int, x: Int, y: Int) -> Unit,
    onStrokeAddPoint: (x: Int, y: Int) -> Unit,
    onStrokeEnd: () -> Unit,
    strokeColor: Color,
    strokeThickness: Float,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    state: DrawingCanvasState = rememberDrawingCanvasState(),
) {
    var viewSize by remember { mutableStateOf(IntSize.Zero) }
    val currentPoints = remember { mutableStateListOf<Offset>() }

    Canvas(
        modifier = modifier
            .fillMaxSize()
            .onSizeChanged { viewSize = it }
            .pointerInput(strokeColor, strokeThickness, enabled) {
                if (!enabled) return@pointerInput
                detectDragGestures(
                    onDragStart = { offset ->
                        currentPoints.clear()
                        val w = viewSize.width.toFloat()
                        val h = viewSize.height.toFloat()
                        if (w <= 0f || h <= 0f) return@detectDragGestures

                        val clamped = Offset(
                            (offset.x / w).coerceIn(0f, 1f),
                            (offset.y / h).coerceIn(0f, 1f)
                        )
                        currentPoints.add(clamped)

                        val colorInt = ((strokeColor.alpha * 255).toInt() shl 24) or
                            ((strokeColor.red * 255).toInt() shl 16) or
                            ((strokeColor.green * 255).toInt() shl 8) or
                            (strokeColor.blue * 255).toInt()
                        val thicknessPx = (strokeThickness * canvasWidth / w).toInt().coerceAtLeast(1)
                        val cx = (offset.x / w * canvasWidth).toInt().coerceIn(0, canvasWidth - 1)
                        val cy = (offset.y / h * canvasHeight).toInt().coerceIn(0, canvasHeight - 1)
                        onStrokeBegin(colorInt, thicknessPx, cx, cy)
                    },
                    onDrag = { change, _ ->
                        val pos = change.position
                        val w = viewSize.width.toFloat()
                        val h = viewSize.height.toFloat()
                        if (w <= 0f || h <= 0f) return@detectDragGestures

                        val clamped = Offset(
                            (pos.x / w).coerceIn(0f, 1f),
                            (pos.y / h).coerceIn(0f, 1f)
                        )
                        currentPoints.add(clamped)

                        val cx = (pos.x / w * canvasWidth).toInt().coerceIn(0, canvasWidth - 1)
                        val cy = (pos.y / h * canvasHeight).toInt().coerceIn(0, canvasHeight - 1)
                        onStrokeAddPoint(cx, cy)
                    },
                    onDragEnd = {
                        if (currentPoints.isNotEmpty()) {
                            state.completedStrokes.add(
                                LocalStroke(
                                    points = currentPoints.toList(),
                                    color = strokeColor,
                                    thickness = strokeThickness
                                )
                            )
                            state.undoneStrokes.clear()
                        }
                        currentPoints.clear()
                        onStrokeEnd()
                    },
                    onDragCancel = {
                        currentPoints.clear()
                        onStrokeEnd()
                    }
                )
            }
    ) {
        val w = size.width
        val h = size.height
        // Draw completed strokes
        for (stroke in state.completedStrokes) {
            if (stroke.points.size < 2) continue
            val path = Path().apply {
                moveTo(stroke.points[0].x * w, stroke.points[0].y * h)
                for (i in 1 until stroke.points.size) {
                    lineTo(stroke.points[i].x * w, stroke.points[i].y * h)
                }
            }
            drawPath(
                path = path,
                color = stroke.color,
                style = Stroke(
                    width = stroke.thickness,
                    cap = StrokeCap.Round,
                    join = StrokeJoin.Round
                )
            )
        }
        // Draw in-progress stroke
        if (currentPoints.size >= 2) {
            val path = Path().apply {
                moveTo(currentPoints[0].x * w, currentPoints[0].y * h)
                for (i in 1 until currentPoints.size) {
                    lineTo(currentPoints[i].x * w, currentPoints[i].y * h)
                }
            }
            drawPath(
                path = path,
                color = strokeColor,
                style = Stroke(
                    width = strokeThickness,
                    cap = StrokeCap.Round,
                    join = StrokeJoin.Round
                )
            )
        }
    }
}
