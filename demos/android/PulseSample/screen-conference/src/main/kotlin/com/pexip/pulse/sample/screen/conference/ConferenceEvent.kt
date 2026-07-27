package com.pexip.pulse.sample.screen.conference

import com.slack.circuit.runtime.CircuitUiEvent

sealed interface ConferenceEvent : CircuitUiEvent {
    data object MuteAudio : ConferenceEvent
    data object MuteVideo : ConferenceEvent
    data object ToggleScreenSharing : ConferenceEvent
    data object FlipCamera : ConferenceEvent
    data object ToggleBlur : ConferenceEvent
    data object ToggleVideoMix : ConferenceEvent
    data object TogglePaint : ConferenceEvent
    data object Chat : ConferenceEvent
    data object Roster : ConferenceEvent
    data object Settings : ConferenceEvent
    data object Disconnect : ConferenceEvent
}
