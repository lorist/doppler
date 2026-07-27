package com.pexip.pulse.sample.screen.chat

import com.pexip.pulse.conference.Message
import com.slack.circuit.runtime.CircuitUiState
import kotlinx.collections.immutable.ImmutableList

data class ChatState(
    val messages: ImmutableList<Message>,
    val onEvent: (ChatEvent) -> Unit,
) : CircuitUiState
