package com.pexip.pulse.sample.screen.roster

import com.pexip.pulse.conference.Participant
import com.slack.circuit.runtime.CircuitUiState
import kotlinx.collections.immutable.ImmutableList

data class RosterState(
    val me: Participant? = null,
    val participants: ImmutableList<Participant>,
    val locked: Boolean = false,
    val allGuestsMuted: Boolean = false,
    val guestsCanUnmute: Boolean = true,
    val onEvent: (RosterEvent) -> Unit,
) : CircuitUiState
