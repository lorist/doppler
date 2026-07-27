package com.pexip.pulse.sample.screen.conference

import android.annotation.SuppressLint
import android.content.res.Configuration.ORIENTATION_PORTRAIT
import androidx.activity.compose.LocalActivity
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.wrapContentSize
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.windowsizeclass.ExperimentalMaterial3WindowSizeClassApi
import androidx.compose.material3.windowsizeclass.WindowSizeClass
import androidx.compose.material3.windowsizeclass.WindowWidthSizeClass
import androidx.compose.material3.windowsizeclass.calculateWindowSizeClass
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import com.pexip.pulse.media.EmbeddedVideoView
import com.pexip.pulse.media.VIDEO_ASPECT_RATIO_LANDSCAPE
import com.pexip.pulse.media.VideoSurface
import com.pexip.pulse.sample.screen.conference.annotation.AnnotationOverlay
import com.pexip.pulse.sample.util.design.BlurButton
import com.pexip.pulse.sample.util.design.CameraButton
import com.pexip.pulse.sample.util.design.ChatButton
import com.pexip.pulse.sample.util.design.DisconnectButton
import com.pexip.pulse.sample.util.design.MicrophoneButton
import com.pexip.pulse.sample.util.design.PaintButton
import com.pexip.pulse.sample.util.design.ProgressOverlay
import com.pexip.pulse.sample.util.design.RosterButton
import com.pexip.pulse.sample.util.design.ScreenShareButton
import com.pexip.pulse.sample.util.design.SettingsButton
import com.pexip.pulse.sample.util.design.TextOverlay
import com.pexip.pulse.sample.util.design.VideoMixButton
import kotlinx.coroutines.FlowPreview
import kotlinx.coroutines.flow.debounce
import kotlin.time.Duration.Companion.milliseconds

@Composable
fun Conference(
    state: ConferenceState,
    modifier: Modifier = Modifier
) {
    MainContent(state, modifier)
    if (state.loading) {
        ProgressOverlay(modifier = modifier)
    }
}

@OptIn(ExperimentalMaterial3WindowSizeClassApi::class)
@Composable
private fun MainContent(
    state: ConferenceState,
    modifier: Modifier = Modifier
) {
    val colorScheme = MaterialTheme.colorScheme
    val isLandscape = LocalConfiguration.current.orientation != ORIENTATION_PORTRAIT
    Surface {
        Box(
            modifier = modifier
                .fillMaxSize()
                .background(colorScheme.background)
        ) {
            if (state.rtmpEnabled) {
                MainVideo(
                    onSurface = state.onSelfVideoSurface,
                    onAspectRatioChange = { },
                    modifier = Modifier.fillMaxSize(),
                    contentAlignment = Alignment.Center,
                )
                // Drawing canvas overlay (touch capture) — present when annotation is acquired.
                // The toolbar panel visibility is controlled separately by isPaintActive.
                // Uses same aspect ratio and centering as the video so coordinates match.
                AnnotationOverlay(
                    state = state.annotationState,
                    modifier = Modifier.align(Alignment.TopCenter)
                )
            } else {
                // Normal mode: show remote video with presentation support
                val windowSizeClass = calculateWindowSizeClass()
                RemoteVideoView(
                    onMainVideoSurface = state.onRemoteVideoSurface,
                    onPresentationVideoSurface = state.onPresentationVideoSurface,
                    presenting = state.remotePresenting,
                    windowSizeClass = windowSizeClass,
                    onAspectRatioChange = { },
                )
            }
            Column(
                modifier = Modifier.fillMaxSize(),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                // Hide TopBar when paint toolbar is shown (it replaces it)
                if (!state.annotationState.isPaintActive) {
                    TopBar(
                        headline = state.conferenceName,
                        text = if (state.rtmpEnabled && isLandscape) state.rtmpDisplayUrl else null,
                        onClick = state.onEvent
                    )
                }
                if (!state.isVideoMuted && !state.rtmpEnabled) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 8.dp),
                        horizontalArrangement = Arrangement.End
                    ) {
                        SelfVideoView(
                            mirror = state.mirrorSelfView,
                            onSurface = state.onSelfVideoSurface,
                            onClick = {
                                state.onEvent(ConferenceEvent.FlipCamera)
                            }
                        )
                    }
                }
                Spacer(
                    modifier = Modifier.weight(1f)
                )
                if (state.rtmpEnabled && !isLandscape) {
                    TextOverlay(
                        text = state.rtmpDisplayUrl,
                        modifier = Modifier.padding(bottom = 16.dp)
                    )
                }
                CallControls(
                    isAudioMuted = state.isAudioMuted,
                    isVideoMuted = state.isVideoMuted,
                    isScreenSharing = state.isScreenSharing,
                    isBlurOn = state.isBlurOn,
                    isVideoMixActive = state.isVideoMixActive,
                    rtmpEnabled = state.rtmpEnabled,
                    isPaintActive = state.annotationState.isPaintActive,
                    onClick = state.onEvent
                )
            }
        }
    }
}

@Composable
private fun TopBar(
    headline: String,
    text: String? = null,
    onClick: (ConferenceEvent) -> Unit
) {
    val colorScheme = MaterialTheme.colorScheme
    Column(
        horizontalAlignment = Alignment.CenterHorizontally,
        modifier = Modifier
            .fillMaxWidth()
            .drawWithContent {
                drawRect(
                    brush = Brush.verticalGradient(
                        0f to colorScheme.background,
                        0.75f to Color.Transparent,
                    ),
                    alpha = 0.8f,
                )
                drawContent()
            }
            .padding(WindowInsets.safeDrawing.asPaddingValues())
            .padding(horizontal = 16.dp)
            .padding(top = 8.dp),
    ) {
        Row(
            horizontalArrangement = Arrangement.spacedBy(16.dp),
            verticalAlignment = Alignment.CenterVertically,
            modifier = Modifier.fillMaxWidth()
        ) {
            Text(
                text = headline,
                style = MaterialTheme.typography.headlineSmall.copy(
                    shadow = Shadow(
                        color = Color.Black.copy(0.4f),
                        offset = Offset(2f, 2f),
                        blurRadius = 4f
                    )
                )
            )
            if (text != null) {
                Spacer(
                    modifier = Modifier.weight(1f)
                )
                Text(
                    text = text,
                    style = MaterialTheme.typography.bodySmall.copy(
                        shadow = Shadow(
                            color = Color.Black.copy(0.4f),
                            offset = Offset(2f, 2f),
                            blurRadius = 4f
                        )
                    )
                )
            }
            Spacer(
                modifier = Modifier.weight(1f)
            )
            ChatButton(
                onClick = { onClick(ConferenceEvent.Chat) }
            )
            RosterButton(
                onClick = { onClick(ConferenceEvent.Roster) }
            )
            SettingsButton(
                onClick = { onClick(ConferenceEvent.Settings) }
            )
        }
    }
}

@Composable
private fun SelfVideoView(
    mirror: Boolean,
    onSurface: (VideoSurface) -> Unit,
    onClick: () -> Unit
) {
    val configuration = LocalConfiguration.current
    val isPortrait = configuration.orientation == ORIENTATION_PORTRAIT
    val size = 150.dp
    EmbeddedVideoView(
        modifier = Modifier
            .width(isPortrait.let { if (it) size * 9/16 else size })
            .height(isPortrait.let { if (it) size else size * 9/16 })
            .border(2.dp, Color.White, RoundedCornerShape(8.dp))
            .clip(RoundedCornerShape(8.dp))
            .clickable { onClick() }
            .background(MaterialTheme.colorScheme.surfaceVariant),
        mirror = mirror,
        onSurface = onSurface
    )
}

@Composable
private fun CallControls(
    isAudioMuted: Boolean,
    isVideoMuted: Boolean,
    isScreenSharing: Boolean,
    isBlurOn: Boolean,
    isVideoMixActive: Boolean,
    rtmpEnabled: Boolean,
    isPaintActive: Boolean,
    onClick: (ConferenceEvent) -> Unit
) {
    val colorScheme = MaterialTheme.colorScheme
    Row(
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(16.dp),
        modifier = Modifier
            .fillMaxWidth()
            .drawWithContent {
                drawRect(
                    brush = Brush.verticalGradient(
                        0f to Color.Transparent,
                        1f to colorScheme.background,
                    ),
                    alpha = 0.8f,
                )
                drawContent()
            }
            .padding(WindowInsets.safeDrawing.asPaddingValues())
            .padding(horizontal = 16.dp)
            .padding(bottom = 8.dp),
    ) {
        Spacer(
            modifier = Modifier.weight(1f)
        )
        MicrophoneButton(
            muted = isAudioMuted,
            onClick = { onClick(ConferenceEvent.MuteAudio) }
        )
        CameraButton(
            muted = isVideoMuted,
            onClick = { onClick(ConferenceEvent.MuteVideo) }
        )
        if (!rtmpEnabled) {
            BlurButton(
                enabled = !isVideoMuted && !isVideoMixActive,
                checked = isBlurOn,
                onClick = { onClick(ConferenceEvent.ToggleBlur) }
            )
            ScreenShareButton(
                capturing = isScreenSharing,
                onClick = { onClick(ConferenceEvent.ToggleScreenSharing) }
            )
            VideoMixButton(
                active = isVideoMixActive,
                enabled = isScreenSharing && !isBlurOn,
                onClick = { onClick(ConferenceEvent.ToggleVideoMix) }
            )
        }
        if (rtmpEnabled) {
            PaintButton(
                onClick = { onClick(ConferenceEvent.TogglePaint) },
                active = isPaintActive
            )
        }
        DisconnectButton(
            onClick = { onClick(ConferenceEvent.Disconnect) }
        )
        Spacer(
            modifier = Modifier.weight(1f)
        )
    }
}

@Composable
private fun RemoteVideoView(
    onMainVideoSurface: (VideoSurface) -> Unit,
    onPresentationVideoSurface: (VideoSurface) -> Unit,
    presenting: Boolean,
    windowSizeClass: WindowSizeClass,
    onAspectRatioChange: (Float) -> Unit,
    modifier: Modifier = Modifier,
) {
    Box(contentAlignment = Alignment.Center, modifier = modifier) {
        val fillAvailableSpace = false
        val mainVideo: @Composable (Modifier, Boolean) -> Unit = { videoModifier, horizontal ->
            MainVideo(
                onSurface = onMainVideoSurface,
                onAspectRatioChange = onAspectRatioChange,
                modifier = videoModifier,
                contentAlignment = when {
                    horizontal && presenting -> Alignment.CenterEnd
                    !horizontal && presenting -> Alignment.BottomCenter
                    else -> Alignment.Center
                },
            )
        }
        val presentation: @Composable (
            Modifier,
            Boolean,
        ) -> Unit = { presentationModifier, horizontal ->
            if (presenting) {
                Presentation(
                    onSurface = onPresentationVideoSurface,
                    modifier = presentationModifier,
                    contentAlignment = when {
                        horizontal -> Alignment.CenterStart
                        !horizontal -> Alignment.TopCenter
                        else -> Alignment.Center
                    },
                )
            }
        }
        when (windowSizeClass.widthSizeClass > WindowWidthSizeClass.Compact) {
            true -> HorizontalVideoLayout(
                fillWidth = fillAvailableSpace,
                mainVideo = mainVideo,
                presentation = presentation,
            )
            else -> VerticalVideoLayout(
                fillHeight = fillAvailableSpace,
                mainVideo = mainVideo,
                presentation = presentation,
            )
        }
    }
}

@Composable
private fun HorizontalVideoLayout(
    fillWidth: Boolean,
    mainVideo: @Composable (Modifier, Boolean) -> Unit,
    presentation: @Composable (Modifier, Boolean) -> Unit,
    modifier: Modifier = Modifier,
) {
    Row(
        horizontalArrangement = Arrangement.Center,
        verticalAlignment = Alignment.CenterVertically,
        modifier = modifier.fillMaxSize(),
    ) {
        val weightModifier = Modifier.weight(1f, fillWidth)
            .clipToBounds()
        mainVideo(weightModifier, true)
        presentation(weightModifier, true)
    }
}

@Composable
private fun VerticalVideoLayout(
    fillHeight: Boolean,
    mainVideo: @Composable (Modifier, Boolean) -> Unit,
    presentation: @Composable (Modifier, Boolean) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
        modifier = modifier.fillMaxSize(),
    ) {
        val weightModifier = Modifier.weight(1f, fillHeight)
            .clipToBounds()
        mainVideo(weightModifier, false)
        presentation(weightModifier, false)
    }
}

@SuppressLint("UnusedBoxWithConstraintsScope")
@OptIn(FlowPreview::class)
@Composable
private fun MainVideo(
    onSurface: (VideoSurface) -> Unit,
    onAspectRatioChange: (Float) -> Unit,
    modifier: Modifier = Modifier,
    contentAlignment: Alignment = Alignment.Center,
) {
    BoxWithConstraints(contentAlignment = contentAlignment, modifier = modifier) {
        val viewportAspectRatio by rememberUpdatedState(maxWidth / maxHeight)
        val currentOnAspectRatioChange by rememberUpdatedState(onAspectRatioChange)
        LaunchedEffect(Unit) {
            snapshotFlow { viewportAspectRatio }
                .debounce(200.milliseconds)
                .collect(currentOnAspectRatioChange)
        }
        EmbeddedVideoView(
            modifier = Modifier
                .wrapContentSize()
                .aspectRatio(VIDEO_ASPECT_RATIO_LANDSCAPE)
                .testTag("main_video"),
            isOpaque = false,
            onSurface = onSurface
        )
    }
}

@SuppressLint("UnusedBoxWithConstraintsScope")
@Composable
private fun Presentation(
    onSurface: (VideoSurface) -> Unit,
    modifier: Modifier = Modifier,
    contentAlignment: Alignment = Alignment.Center,
) {
    BoxWithConstraints(
        contentAlignment = contentAlignment,
        modifier = modifier,
    ) {
        EmbeddedVideoView(
            modifier = Modifier
                .wrapContentSize()
                .aspectRatio(VIDEO_ASPECT_RATIO_LANDSCAPE)
                .testTag("presentation"),
            isOpaque = false,
            onSurface = onSurface
        )
    }
}


@ExperimentalMaterial3WindowSizeClassApi
@Composable
private fun calculateWindowSizeClass(): WindowSizeClass =
    calculateWindowSizeClass(checkNotNull(LocalActivity.current))
