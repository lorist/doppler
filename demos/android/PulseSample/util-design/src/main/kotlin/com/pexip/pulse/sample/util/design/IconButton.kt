package com.pexip.pulse.sample.util.design

import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.IconButtonColors
import androidx.compose.material3.IconButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp

@Composable
fun IconButton(
    onClick: () -> Unit,
    imageVector: ImageVector,
    contentDescription: String,
    enabled: Boolean = true,
    colors: IconButtonColors = IconButtonDefaults.iconButtonColors()
) {
    IconButton(
        onClick = onClick,
        modifier = Modifier.size(48.dp),
        enabled = enabled,
        colors = colors,
        content = {
            Icon(
                imageVector = imageVector,
                contentDescription = contentDescription,
                modifier = Modifier.size(24.dp)
            )
        }
    )
}

@Composable
internal fun mediaButtonColors(
    checked: Boolean
): IconButtonColors = IconButtonDefaults.filledIconButtonColors(
    containerColor = if (checked) Color.White else MaterialTheme.colorScheme.surfaceContainer.copy(alpha = 0.7f),
    contentColor = if (checked) MaterialTheme.colorScheme.surfaceContainer else Color.White
)
