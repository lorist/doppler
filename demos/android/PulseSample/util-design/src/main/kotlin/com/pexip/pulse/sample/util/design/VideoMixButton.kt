package com.pexip.pulse.sample.util.design

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.PictureInPicture
import androidx.compose.runtime.Composable

@Composable
fun VideoMixButton(
    onClick: () -> Unit,
    active: Boolean,
    enabled: Boolean = true
) {
    IconButton(
        onClick = onClick,
        imageVector = Icons.Filled.PictureInPicture,
        contentDescription = "Video Mix",
        enabled = enabled,
        colors = mediaButtonColors(checked = active)
    )
}
