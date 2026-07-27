package com.pexip.pulse.sample.screen.roster

import com.pexip.pulse.conference.Participant
import com.slack.circuit.runtime.CircuitUiEvent

sealed interface RosterEvent : CircuitUiEvent {
    data object Back : RosterEvent
    data object Lock: RosterEvent
    data object Unlock: RosterEvent
    data object MuteAllGuests : RosterEvent
    data object UnmuteAllGuests : RosterEvent
    data object AllowGuestsToUnmute : RosterEvent
    data object DisallowGuestsToUnmute : RosterEvent
    data class LowerHand(val participant: Participant) : RosterEvent
    data class MuteAudio(val participant: Participant) : RosterEvent
    data class UnmuteAudio(val participant: Participant) : RosterEvent
    data class AddSpotlight(val participant: Participant) : RosterEvent
    data class RemoveSpotlight(val participant: Participant) : RosterEvent
    data class MakeGuest(val participant: Participant) : RosterEvent
    data class MakeHost(val participant: Participant) : RosterEvent
    data class Disconnect(val participant: Participant) : RosterEvent
}
