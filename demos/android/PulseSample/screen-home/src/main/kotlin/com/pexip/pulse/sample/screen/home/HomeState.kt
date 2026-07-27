package com.pexip.pulse.sample.screen.home

import com.slack.circuit.runtime.CircuitUiState

data class HomeState(
    val displayName: String,
    val conferenceAlias: String,
    val serverAddress: String,
    val pin: String,
    val rtmpEnabled: Boolean,
    val onEvent: (HomeEvent) -> Unit
) : CircuitUiState
