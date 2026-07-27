package com.pexip.pulse.sample.screen.home

import com.slack.circuit.runtime.CircuitUiEvent
import com.slack.circuit.runtime.screen.Screen
import kotlinx.parcelize.Parcelize

@Parcelize
data object HomeScreen : Screen {
    sealed interface Event : CircuitUiEvent
}
