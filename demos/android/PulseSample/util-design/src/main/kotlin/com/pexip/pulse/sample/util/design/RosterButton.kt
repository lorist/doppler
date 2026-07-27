package com.pexip.pulse.sample.util.design

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.PeopleAlt
import androidx.compose.runtime.Composable

@Composable
fun RosterButton(
    onClick: () -> Unit,
    enabled: Boolean = true
) {
    IconButton(
        onClick = onClick,
        imageVector = Icons.Filled.PeopleAlt,
        contentDescription = "Participants",
        enabled = enabled,
        colors = mediaButtonColors(checked = false)
    )
}
