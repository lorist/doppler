package com.pexip.pulse.sample.util.design

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.FlipCameraAndroid
import androidx.compose.runtime.Composable

@Composable
fun FlipCameraButton(
    onClick: () -> Unit,
    enabled: Boolean = true
) {
    IconButton(
        onClick = onClick,
        imageVector = Icons.Filled.FlipCameraAndroid,
        contentDescription = "Flip Camera",
        enabled = enabled,
        colors = mediaButtonColors(checked = false)
    )
}
