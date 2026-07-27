package com.pexip.pulse.sample.screen.roster

import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.runtime.rememberCoroutineScope
import com.pexip.pulse.conference.Participant
import com.pexip.pulse.conference.Roster
import com.pexip.pulse.core.Pulse
import com.slack.circuit.runtime.Navigator
import com.slack.circuit.runtime.presenter.Presenter
import kotlinx.collections.immutable.ImmutableList
import kotlinx.collections.immutable.persistentListOf
import kotlinx.coroutines.launch
import javax.inject.Inject
import javax.inject.Singleton

class RosterPresenter(
    private val navigator: Navigator,
    private val roster: Roster
) : Presenter<RosterState> {

    @Composable
    override fun present(): RosterState {
        val scope = rememberCoroutineScope()
        val participants by produceParticipantsState(roster)
        val me by roster.me.collectAsState()
        val locked by roster.locked.collectAsState()
        val allGuestsMuted by roster.allGuestsMuted.collectAsState()
        val guestsCanUnmute by roster.guestsCanUnmute.collectAsState()
        return RosterState(
            me = me,
            participants = participants,
            locked = locked,
            allGuestsMuted = allGuestsMuted,
            guestsCanUnmute = guestsCanUnmute ?: true,
            onEvent = { event ->
                when (event) {
                    is RosterEvent.Back -> navigator.pop()

                    RosterEvent.Lock -> scope.launch {
                        roster.lock()
                    }

                    RosterEvent.Unlock -> scope.launch {
                        roster.unlock()
                    }

                    RosterEvent.MuteAllGuests -> scope.launch {
                        roster.muteAllGuests()
                    }

                    RosterEvent.UnmuteAllGuests -> scope.launch {
                        roster.unmuteAllGuests()
                    }

                    is RosterEvent.AllowGuestsToUnmute -> scope.launch {
                        roster.allowGuestsToUnmute()
                    }

                    RosterEvent.DisallowGuestsToUnmute -> scope.launch {
                        roster.disallowGuestsToUnmute()
                    }

                    is RosterEvent.LowerHand -> scope.launch {
                        roster.lowerHand(event.participant.id)
                    }

                    is RosterEvent.AddSpotlight -> scope.launch {
                        roster.spotlight(event.participant.id)
                    }

                    is RosterEvent.RemoveSpotlight -> scope.launch {
                        roster.unspotlight(event.participant.id)
                    }

                    is RosterEvent.MuteAudio -> scope.launch {
                        roster.mute(event.participant.id)
                    }

                    is RosterEvent.UnmuteAudio -> scope.launch {
                        roster.unmute(event.participant.id)
                    }

                    is RosterEvent.MakeGuest -> scope.launch {
                        roster.makeGuest(event.participant.id)
                    }

                    is RosterEvent.MakeHost -> scope.launch {
                        roster.makeHost(event.participant.id)
                    }

                    is RosterEvent.Disconnect -> scope.launch {
                        roster.disconnect(event.participant.id)
                    }
                }
            }
        )
    }

    @Composable
    private fun produceParticipantsState(roster: Roster) =
        produceState<ImmutableList<Participant>>(persistentListOf()) {
            roster.participants.collect {
                value = persistentListOf(*it.toTypedArray())
            }
    }

    @Singleton
    class Factory @Inject constructor(private val pulse: Pulse) {
        fun create(navigator: Navigator): Presenter<RosterState> =
            RosterPresenter(
                navigator = navigator,
                roster = pulse.conference.roster,
            )
    }
}
