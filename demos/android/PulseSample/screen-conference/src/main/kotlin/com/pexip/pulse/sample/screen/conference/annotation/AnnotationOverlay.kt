package com.pexip.pulse.sample.screen.conference.annotation

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.wrapContentSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.pexip.pulse.media.VIDEO_ASPECT_RATIO_LANDSCAPE

/**
 * Annotation overlay UI that renders the drawing canvas and paint toolbar.
 * Only shown when [state] indicates annotations are active.
 */
@Composable
fun AnnotationOverlay(
    state: AnnotationState,
    modifier: Modifier = Modifier
) {
    if (!state.isActive) return

    var paintColor by remember { mutableStateOf(DefaultStrokeColor) }
    var paintThickness by remember { mutableFloatStateOf(DefaultStrokeThickness) }
    val canvasState = rememberDrawingCanvasState()

    Box(
        modifier = modifier.fillMaxSize(),
        contentAlignment = Alignment.Center
    ) {
        DrawingCanvas(
            canvasWidth = state.canvasWidth,
            canvasHeight = state.canvasHeight,
            onStrokeBegin = { color, thickness, x, y ->
                state.onEvent(AnnotationEvent.StrokeBegin(color, thickness, x, y))
            },
            onStrokeAddPoint = { x, y ->
                state.onEvent(AnnotationEvent.StrokeAddPoint(x, y))
            },
            onStrokeEnd = {
                state.onEvent(AnnotationEvent.StrokeEnd)
            },
            strokeColor = paintColor,
            strokeThickness = paintThickness,
            enabled = state.isPaintActive,
            state = canvasState,
            modifier = Modifier
                .wrapContentSize()
                .aspectRatio(VIDEO_ASPECT_RATIO_LANDSCAPE)
        )
    }
    if (state.isPaintActive) {
        PaintToolbar(
            selectedColor = paintColor,
            onColorSelected = { paintColor = it },
            thickness = paintThickness,
            onThicknessChange = { paintThickness = it },
            onUndo = {
                canvasState.undo()
                state.onEvent(AnnotationEvent.Undo)
            },
            onRedo = {
                canvasState.redo()
                state.onEvent(AnnotationEvent.Redo)
            },
            onClear = {
                canvasState.clear()
                state.onEvent(AnnotationEvent.Clear)
            },
            modifier = Modifier
                .padding(WindowInsets.safeDrawing.asPaddingValues())
                .padding(horizontal = 8.dp)
                .padding(top = 8.dp)
        )
    }
}
