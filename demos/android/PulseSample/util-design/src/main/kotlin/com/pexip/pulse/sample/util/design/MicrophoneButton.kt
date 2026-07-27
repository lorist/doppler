package com.pexip.pulse.sample.util.design

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.MicOff
import androidx.compose.runtime.Composable

@Composable
fun MicrophoneButton(
    muted: Boolean,
    enabled: Boolean = true,
    onClick: () -> Unit,
) {
    IconButton(
        onClick = onClick,
        imageVector = if (muted) Icons.Filled.MicOff else Icons.Filled.Mic,
        contentDescription = "Microphone",
        enabled = enabled,
        colors = mediaButtonColors(checked = muted)
    )
}
