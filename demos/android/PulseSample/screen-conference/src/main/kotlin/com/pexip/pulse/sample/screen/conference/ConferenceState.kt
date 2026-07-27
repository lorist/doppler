package com.pexip.pulse.sample.screen.conference

import com.pexip.pulse.media.VideoSurface
import com.pexip.pulse.sample.screen.conference.annotation.AnnotationState
import com.slack.circuit.runtime.CircuitUiState

data class ConferenceState(
    var conferenceName: String,
    val rtmpEnabled: Boolean,
    val rtmpDisplayUrl: String,
    val isAudioMuted: Boolean,
    val isVideoMuted: Boolean,
    val isScreenSharing: Boolean,
    val remotePresenting: Boolean,
    val mirrorSelfView: Boolean,
    val isBlurOn: Boolean,
    val isVideoMixActive: Boolean,
    val annotationState: AnnotationState,
    val loading: Boolean,
    val onSelfVideoSurface: (VideoSurface) -> Unit,
    val onRemoteVideoSurface: (VideoSurface) -> Unit,
    val onPresentationVideoSurface: (VideoSurface) -> Unit,
    val onEvent: (ConferenceEvent) -> Unit
) : CircuitUiState
