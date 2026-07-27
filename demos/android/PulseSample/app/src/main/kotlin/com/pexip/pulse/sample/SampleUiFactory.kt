package com.pexip.pulse.sample

import com.pexip.pulse.sample.screen.chat.Chat
import com.pexip.pulse.sample.screen.chat.ChatScreen
import com.pexip.pulse.sample.screen.chat.ChatState
import com.pexip.pulse.sample.screen.conference.Conference
import com.pexip.pulse.sample.screen.conference.ConferenceScreen
import com.pexip.pulse.sample.screen.conference.ConferenceState
import com.pexip.pulse.sample.screen.home.Home
import com.pexip.pulse.sample.screen.home.HomeScreen
import com.pexip.pulse.sample.screen.home.HomeState
import com.pexip.pulse.sample.screen.preflight.PreFlight
import com.pexip.pulse.sample.screen.preflight.PreFlightScreen
import com.pexip.pulse.sample.screen.preflight.PreFlightState
import com.pexip.pulse.sample.screen.roster.Roster
import com.pexip.pulse.sample.screen.roster.RosterScreen
import com.pexip.pulse.sample.screen.roster.RosterState
import com.pexip.pulse.sample.screen.settings.Settings
import com.pexip.pulse.sample.screen.settings.SettingsScreen
import com.pexip.pulse.sample.screen.settings.SettingsState
import com.pexip.pulse.sample.screen.statistics.MediaStatistics
import com.pexip.pulse.sample.screen.statistics.MediaStatisticsScreen
import com.pexip.pulse.sample.screen.statistics.MediaStatisticsState
import com.slack.circuit.runtime.CircuitContext
import com.slack.circuit.runtime.screen.Screen
import com.slack.circuit.runtime.ui.Ui
import com.slack.circuit.runtime.ui.ui
import javax.inject.Inject
import javax.inject.Singleton


@Singleton
class SampleUiFactory @Inject constructor() : Ui.Factory {

    override fun create(screen: Screen, context: CircuitContext): Ui<*>? = when (screen) {
        is HomeScreen -> ui<HomeState> { state, modifier ->
            Home(state = state, modifier = modifier)
        }
        is PreFlightScreen -> ui<PreFlightState> { state, modifier ->
            PreFlight(state = state, modifier = modifier)
        }
        is ConferenceScreen -> ui<ConferenceState> { state, modifier ->
            Conference(state = state, modifier = modifier)
        }
        is SettingsScreen -> ui<SettingsState> { state, modifier ->
            Settings(state = state, modifier = modifier)
        }
        is MediaStatisticsScreen -> ui<MediaStatisticsState> { state, modifier ->
            MediaStatistics(state = state, modifier = modifier)
        }
        is RosterScreen -> ui<RosterState> { state, modifier ->
            Roster(state = state, modifier = modifier)
        }
        is ChatScreen -> ui<ChatState> { state, modifier ->
            Chat(state = state, modifier = modifier)
        }
        else -> null
    }
}
