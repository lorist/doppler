package com.pexip.pulse.sample.util.design

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ScreenShare
import androidx.compose.runtime.Composable

@Composable
fun ScreenShareButton(
    onClick: () -> Unit,
    capturing: Boolean,
    enabled: Boolean = true
) {
    IconButton(
        onClick = onClick,
        imageVector = Icons.AutoMirrored.Filled.ScreenShare,
        contentDescription = "Screen Share",
        enabled = enabled,
        colors = mediaButtonColors(checked = capturing)
    )
}
