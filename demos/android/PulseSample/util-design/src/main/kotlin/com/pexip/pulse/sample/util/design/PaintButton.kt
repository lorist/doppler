package com.pexip.pulse.sample.util.design

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Brush
import androidx.compose.runtime.Composable

@Composable
fun PaintButton(
    onClick: () -> Unit,
    active: Boolean = false,
    enabled: Boolean = true
) {
    IconButton(
        onClick = onClick,
        imageVector = Icons.Filled.Brush,
        contentDescription = "Paint",
        enabled = enabled,
        colors = mediaButtonColors(checked = active)
    )
}
