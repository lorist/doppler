package com.pexip.pulse.sample.screen.settings

import com.pexip.pulse.media.VideoDevice
import com.pexip.sdk.media.AudioDevice
import com.slack.circuit.runtime.CircuitUiState

data class SettingsState(
    val audioDevices: List<AudioDevice>,
    val currentAudioDevice: AudioDevice?,
    val videoDevices: List<VideoDevice>,
    val currentVideoDevice: VideoDevice?,
    val rtmpEnabled: Boolean,
    val onEvent: (SettingsEvent) -> Unit
) : CircuitUiState
