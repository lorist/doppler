package com.pexip.pulse.sample.screen.home

import com.slack.circuit.runtime.CircuitUiEvent

sealed interface HomeEvent : CircuitUiEvent {
    data class DisplayNameChanged(val value: String) : HomeEvent
    data class ConferenceAliasChanged(val value: String) : HomeEvent
    data class ServerAddressChanged(val value: String) : HomeEvent
    data class PinChanged(val value: String) : HomeEvent
    data object ToggleRtmp : HomeEvent
    data object JoinConference : HomeEvent
}
