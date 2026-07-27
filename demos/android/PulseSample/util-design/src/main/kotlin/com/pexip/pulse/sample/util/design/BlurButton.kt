package com.pexip.pulse.sample.util.design

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.BlurOff
import androidx.compose.material.icons.filled.BlurOn
import androidx.compose.runtime.Composable

@Composable
fun BlurButton(
    onClick: () -> Unit,
    checked: Boolean,
    enabled: Boolean = true
) {
    IconButton(
        onClick = onClick,
        imageVector = if (checked) Icons.Filled.BlurOn else Icons.Filled.BlurOff,
        contentDescription = "Background Blur",
        enabled = enabled,
        colors = mediaButtonColors(checked = checked)
    )
}
