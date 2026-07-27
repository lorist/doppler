package com.pexip.pulse.sample.util.design

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ChatBubble
import androidx.compose.runtime.Composable

@Composable
fun ChatButton(
    onClick: () -> Unit,
    enabled: Boolean = true
) {
    IconButton(
        onClick = onClick,
        imageVector = Icons.Filled.ChatBubble,
        contentDescription = "Chat",
        enabled = enabled,
        colors = mediaButtonColors(checked = false)
    )
}
