package com.pexip.pulse.sample.screen.settings

import com.pexip.pulse.media.VideoDevice
import com.pexip.sdk.media.AudioDevice
import com.slack.circuit.runtime.CircuitUiEvent

sealed interface SettingsEvent : CircuitUiEvent {
    data class AudioDeviceSelected(val device: AudioDevice?) : SettingsEvent
    data class VideoDeviceSelected(val device: VideoDevice?) : SettingsEvent
    data object MediaStatistics : SettingsEvent
    data object Back : SettingsEvent
}
