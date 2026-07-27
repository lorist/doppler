package com.pexip.pulse.sample.screen.preflight

import com.slack.circuit.runtime.screen.Screen
import kotlinx.parcelize.Parcelize

@Parcelize
data class PreFlightScreen(
    val displayName: String,
    val conferenceAlias: String,
    val serverAddress: String,
    val pin: String
) : Screen
