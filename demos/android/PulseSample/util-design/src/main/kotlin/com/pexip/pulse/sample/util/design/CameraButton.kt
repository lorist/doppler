package com.pexip.pulse.sample.util.design

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Videocam
import androidx.compose.material.icons.filled.VideocamOff
import androidx.compose.runtime.Composable

@Composable
fun CameraButton(
    muted: Boolean,
    enabled: Boolean = true,
    onClick: () -> Unit,
) {
    IconButton(
        onClick = onClick,
        imageVector = if (muted) Icons.Filled.VideocamOff else Icons.Filled.Videocam,
        contentDescription = "Camera",
        enabled = enabled,
        colors = mediaButtonColors(checked = muted)
    )
}
