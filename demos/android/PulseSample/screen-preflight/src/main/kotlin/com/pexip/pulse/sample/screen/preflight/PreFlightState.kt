package com.pexip.pulse.sample.screen.preflight

import android.net.Uri
import com.slack.circuit.runtime.CircuitUiState
import com.pexip.pulse.conference.SsoProvider
import com.pexip.pulse.media.VideoSurface

data class PreFlightState(
    val displayName: String,
    val conferenceAlias: String,
    val serverAddress: String,
    val pin: String,
    val rtmpEnabled: Boolean,
    val rtmpDisplayUrl: String,
    val mirrorSelfView: Boolean,
    val isAudioMuted: Boolean,
    val isVideoMuted: Boolean,
    val isBlurOn: Boolean,
    val connecting: Boolean,
    val ssoProviders: List<SsoProvider> = emptyList(),
    val ssoRedirectUrl: Uri? = null,
    val errorMessage: String? = null,
    val onSelfVideoSurface: (VideoSurface) -> Unit,
    val onEvent: (PreFlightEvent) -> Unit
) : CircuitUiState
