package com.pexip.pulse.sample.screen.chat

import com.slack.circuit.runtime.CircuitUiEvent

sealed interface ChatEvent : CircuitUiEvent {
    data object Back : ChatEvent
    data class SendMessage(val text: String) : ChatEvent
}
