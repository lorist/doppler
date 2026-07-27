package com.pexip.pulse.sample.screen.conference.annotation

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Redo
import androidx.compose.material.icons.automirrored.filled.Undo
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp

/**
 * Default stroke color for the drawing canvas.
 */
val DefaultStrokeColor = Color.Red

/**
 * Default stroke thickness in display pixels.
 */
const val MinStrokeThickness = 2f
const val MaxStrokeThickness = 20f
const val DefaultStrokeThickness = 6f

/**
 * Available paint colors for the annotation toolbar.
 */
val PaintColors = listOf(
    DefaultStrokeColor,
    Color.Blue,
    Color.Yellow,
    Color.White,
    Color.Black,
)

/**
 * Toolbar for paint tools: color selection, thickness slider, undo/redo/clear.
 *
 * @param selectedColor Currently selected color.
 * @param onColorSelected Callback when a color is tapped.
 * @param thickness Current stroke thickness.
 * @param onThicknessChange Callback when thickness slider changes.
 * @param onUndo Undo last stroke.
 * @param onRedo Redo last undone stroke.
 * @param onClear Clear all strokes.
 * @param modifier Modifier for the toolbar.
 */
@Composable
fun PaintToolbar(
    selectedColor: Color,
    onColorSelected: (Color) -> Unit,
    thickness: Float,
    onThicknessChange: (Float) -> Unit,
    onUndo: () -> Unit,
    onRedo: () -> Unit,
    onClear: () -> Unit,
    modifier: Modifier = Modifier
) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .background(
                MaterialTheme.colorScheme.surfaceContainer.copy(alpha = 0.85f),
                RoundedCornerShape(12.dp)
            )
            .padding(horizontal = 8.dp, vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(4.dp)
    ) {
        PaintColors.forEach { color ->
            ColorSwatch(
                color = color,
                selected = color == selectedColor,
                onClick = { onColorSelected(color) }
            )
        }

        Slider(
            value = thickness,
            onValueChange = onThicknessChange,
            valueRange = MinStrokeThickness..MaxStrokeThickness,
            modifier = Modifier.weight(1f).padding(horizontal = 4.dp)
        )

        ToolbarIconButton(
            onClick = onUndo,
            imageVector = Icons.AutoMirrored.Filled.Undo,
            contentDescription = "Undo"
        )

        ToolbarIconButton(
            onClick = onRedo,
            imageVector = Icons.AutoMirrored.Filled.Redo,
            contentDescription = "Redo"
        )

        ToolbarIconButton(
            onClick = onClear,
            imageVector = Icons.Filled.Delete,
            contentDescription = "Clear"
        )
    }
}

@Composable
private fun ToolbarIconButton(
    onClick: () -> Unit,
    imageVector: ImageVector,
    contentDescription: String
) {
    IconButton(onClick = onClick, modifier = Modifier.size(36.dp)) {
        Icon(
            imageVector = imageVector,
            contentDescription = contentDescription,
            tint = Color.White
        )
    }
}

@Composable
private fun ColorSwatch(
    color: Color,
    selected: Boolean,
    onClick: () -> Unit
) {
    Box(
        modifier = Modifier
            .size(28.dp)
            .clip(CircleShape)
            .background(color)
            .then(
                if (selected) Modifier.border(2.dp, Color.White, CircleShape)
                else Modifier
            )
            .clickable(onClick = onClick)
    )
}
