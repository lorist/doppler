package com.pexip.pulse.sample.screen.preflight

import android.content.Context
import android.content.Intent
import android.content.res.Configuration.ORIENTATION_LANDSCAPE
import android.net.Uri
import androidx.activity.compose.LocalActivity
import androidx.browser.customtabs.CustomTabsIntent
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.layout.wrapContentSize
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.app.OnNewIntentProvider
import androidx.core.util.Consumer
import com.pexip.pulse.conference.SsoProvider
import com.pexip.pulse.media.EmbeddedVideoView
import com.pexip.pulse.media.VIDEO_ASPECT_RATIO_LANDSCAPE
import com.pexip.pulse.media.VideoSurface
import com.pexip.pulse.media.getVideoAspectRatioForLocalConfiguration
import com.pexip.pulse.sample.util.design.BackButton
import com.pexip.pulse.sample.util.design.BlurButton
import com.pexip.pulse.sample.util.design.CameraButton
import com.pexip.pulse.sample.util.design.FlipCameraButton
import com.pexip.pulse.sample.util.design.MicrophoneButton
import com.pexip.pulse.sample.util.design.ProgressOverlay
import com.pexip.pulse.sample.util.design.SettingsButton
import com.pexip.pulse.sample.util.design.TextOverlay

@Composable
fun PreFlight(
    state: PreFlightState,
    modifier: Modifier = Modifier
) {
    MainContent(state, modifier)

    if (state.ssoProviders.isNotEmpty()) {
        SsoProviderPicker(
            providers = state.ssoProviders,
            onEvent = state.onEvent
        )
    } else if (state.connecting || state.ssoRedirectUrl != null) {
        ProgressOverlay(
            onCancel = { state.onEvent(PreFlightEvent.Cancel) },
            modifier = modifier
        )
    }

    if (state.errorMessage != null) {
        ErrorAlert(
            message = state.errorMessage,
            onEvent = state.onEvent
        )
    }

    SsoAuth(
        redirectUrl = state.ssoRedirectUrl,
        onEvent = state.onEvent
    )
}

@Composable
private fun MainContent(
    state: PreFlightState,
    modifier: Modifier = Modifier
) {
    val configuration = LocalConfiguration.current
    val isLandscape = configuration.orientation == ORIENTATION_LANDSCAPE
    Surface {
        Column(
            modifier = modifier
                .fillMaxSize()
                .windowInsetsPadding(WindowInsets.safeDrawing)
        ) {
            TopBar(
                onClick = state.onEvent
            )
            if (isLandscape) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(16.dp, Alignment.CenterHorizontally),
                    modifier = Modifier.fillMaxSize().padding(16.dp)
                ) {
                    VideoPreviewBox(state)
                    Button(
                        onClick = { state.onEvent(PreFlightEvent.Join) },
                        modifier = Modifier.width(160.dp),
                        content = { Text("Join") }
                    )
                }
            } else {
                Column(
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(16.dp),
                    modifier = Modifier.fillMaxSize().padding(16.dp)
                ) {
                    if (state.rtmpEnabled) {
                        Spacer(modifier = Modifier.weight(1f))
                    }
                    VideoPreviewBox(state)
                    Button(
                        onClick = { state.onEvent(PreFlightEvent.Join) },
                        modifier = Modifier.fillMaxWidth().padding(top = 16.dp),
                        content = { Text("Join") }
                    )
                    if (state.rtmpEnabled) {
                        Spacer(modifier = Modifier.weight(1f))
                    }
                }
            }
        }
    }
}

@Composable
private fun VideoPreviewBox(state: PreFlightState) {
    Box(
        modifier = Modifier
            .wrapContentSize()
            .clip(RoundedCornerShape(16.dp))
            .background(MaterialTheme.colorScheme.surfaceVariant),
        contentAlignment = Alignment.BottomCenter
    ) {
        if (state.rtmpEnabled) {
            MainVideoPreview(onSurface = state.onSelfVideoSurface)
            TextOverlay(
                text = state.rtmpDisplayUrl,
                modifier = Modifier.align(Alignment.TopCenter).padding(top = 8.dp)
            )
        } else {
            SelfVideoView(
                mirror = state.mirrorSelfView,
                muted = state.isVideoMuted,
                onSurface = state.onSelfVideoSurface
            )
        }
        MediaControls(
            isAudioMuted = state.isAudioMuted,
            isVideoMuted = state.isVideoMuted,
            isBlurOn = state.isBlurOn,
            rtmpEnabled = state.rtmpEnabled,
            onClick = state.onEvent
        )
    }
}

@Composable
private fun TopBar(
    onClick: (PreFlightEvent) -> Unit
) {
    Row(
        horizontalArrangement = Arrangement.spacedBy(16.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        BackButton(
            onClick = { onClick(PreFlightEvent.Back) }
        )
        Text(
            text = "Preview",
            style = MaterialTheme.typography.headlineSmall
        )
        Spacer(
            modifier = Modifier.weight(1f)
        )
        SettingsButton(
            onClick = { onClick(PreFlightEvent.Settings) }
        )
    }
}

@Composable
private fun MainVideoPreview(
    onSurface: (VideoSurface) -> Unit
) {
    EmbeddedVideoView(
        modifier = Modifier
            .aspectRatio(VIDEO_ASPECT_RATIO_LANDSCAPE),
        isOpaque = false,
        onSurface = onSurface
    )
}

@Composable
private fun SelfVideoView(
    mirror: Boolean,
    muted: Boolean,
    onSurface: (VideoSurface) -> Unit
) {
    val colorScheme = MaterialTheme.colorScheme
    EmbeddedVideoView(
        modifier = Modifier
            .aspectRatio(getVideoAspectRatioForLocalConfiguration())
            .drawWithContent {
                if (!muted) {
                    drawContent()
                }
                drawRect(
                    brush = Brush.verticalGradient(
                        0.75f to Color.Transparent,
                        1f to colorScheme.background,
                    ),
                    alpha = 0.8f,
                )
            },
        mirror = mirror,
        onSurface = onSurface
    )
}

@Composable
private fun MediaControls(
    isAudioMuted: Boolean,
    isVideoMuted: Boolean,
    isBlurOn: Boolean,
    rtmpEnabled: Boolean,
    onClick: (PreFlightEvent) -> Unit
) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(16.dp),
        modifier = Modifier.padding(16.dp)
    ) {
        MicrophoneButton(
            muted = isAudioMuted,
            onClick = { onClick(PreFlightEvent.MuteAudio) }
        )
        CameraButton(
            muted = isVideoMuted,
            onClick = { onClick(PreFlightEvent.MuteVideo) }
        )
        if (!rtmpEnabled) {
            FlipCameraButton(
                enabled = !isVideoMuted,
                onClick = { onClick(PreFlightEvent.FlipCamera) }
            )
            BlurButton(
                enabled = !isVideoMuted,
                checked = isBlurOn,
                onClick = { onClick(PreFlightEvent.ToggleBlur) }
            )
        }
    }
}

@Composable
private fun ErrorAlert(
    message: String,
    onEvent: (PreFlightEvent) -> Unit,
) {
    AlertDialog(
        onDismissRequest = { onEvent(PreFlightEvent.DismissErrorAlert) },
        title = { Text("Error") },
        text = { Text(message) },
        confirmButton = {
            Button(
                onClick = { onEvent(PreFlightEvent.DismissErrorAlert) }
            ) {
                Text("OK")
            }
        }
    )
}

@Composable
private fun SsoProviderPicker(
    providers: List<SsoProvider>,
    onEvent: (PreFlightEvent) -> Unit,
    modifier: Modifier = Modifier
) {
    Box(
        modifier = modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background),
        contentAlignment = Alignment.Center
    ) {
        Column(
            modifier = Modifier.width(8.dp * 70).padding(16.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            providers.forEach { provider ->
                Button(
                    onClick = { onEvent(PreFlightEvent.SelectSsoProvider(provider)) },
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text(provider.name)
                }
            }
            TextButton(
                onClick = { onEvent(PreFlightEvent.DismissSsoProviderPicker) },
                modifier = Modifier.fillMaxWidth()
            ) {
                Text("Cancel")
            }
        }
    }
}

@Composable
private fun SsoAuth(
    redirectUrl: Uri?,
    onEvent: (PreFlightEvent) -> Unit,
) {
    OnNewIntentHandler {
        val uri = it.data
        if (uri != null) {
            onEvent(PreFlightEvent.HandleSsoData(uri))
        }
    }
    val context = LocalContext.current
    if (redirectUrl != null) {
        LaunchedEffect(context, redirectUrl) {
            context.launchUrl(redirectUrl) {
                setShowTitle(true)
            }
        }
    }
}

@Composable
private fun OnNewIntentHandler(onIntent: (Intent) -> Unit) {
    val provider = checkNotNull(LocalActivity.current as? OnNewIntentProvider)
    val currentOnIntent by rememberUpdatedState(onIntent)
    DisposableEffect(provider) {
        val listener = Consumer(currentOnIntent)
        provider.addOnNewIntentListener(listener)
        onDispose { provider.removeOnNewIntentListener(listener) }
    }
}

private inline fun CustomTabsIntent(block: CustomTabsIntent.Builder.() -> Unit) =
    CustomTabsIntent.Builder().apply(block).build()

private inline fun Context.launchUrl(url: Uri, block: CustomTabsIntent.Builder.() -> Unit = { }) {
    val builder = CustomTabsIntent(block)
    builder.launchUrl(this, url)
}
