package com.pexip.pulse.sample

import com.pexip.pulse.sample.screen.chat.ChatPresenter
import com.pexip.pulse.sample.screen.chat.ChatScreen
import com.pexip.pulse.sample.screen.conference.ConferencePresenter
import com.pexip.pulse.sample.screen.conference.ConferenceScreen
import com.pexip.pulse.sample.screen.home.HomePresenter
import com.pexip.pulse.sample.screen.home.HomeScreen
import com.pexip.pulse.sample.screen.preflight.PreFlightPresenter
import com.pexip.pulse.sample.screen.preflight.PreFlightScreen
import com.pexip.pulse.sample.screen.roster.RosterPresenter
import com.pexip.pulse.sample.screen.roster.RosterScreen
import com.pexip.pulse.sample.screen.settings.SettingsPresenter
import com.pexip.pulse.sample.screen.settings.SettingsScreen
import com.pexip.pulse.sample.screen.statistics.MediaStatisticsPresenter
import com.pexip.pulse.sample.screen.statistics.MediaStatisticsScreen
import com.slack.circuit.runtime.CircuitContext
import com.slack.circuit.runtime.Navigator
import com.slack.circuit.runtime.presenter.Presenter
import com.slack.circuit.runtime.screen.Screen
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class SamplePresenterFactory @Inject constructor(
    private val homePresenterFactory: HomePresenter.Factory,
    private val preFlightPresenterFactory: PreFlightPresenter.Factory,
    private val conferenceScreenFactory: ConferencePresenter.Factory,
    private val settingsScreenFactory: SettingsPresenter.Factory,
    private val mediaStatisticsScreenFactory: MediaStatisticsPresenter.Factory,
    private val rosterScreenFactory: RosterPresenter.Factory,
    private val chatScreenFactory: ChatPresenter.Factory,
) : Presenter.Factory {

    override fun create(
        screen: Screen,
        navigator: Navigator,
        context: CircuitContext
    ): Presenter<*>? = when (screen) {
        is HomeScreen -> homePresenterFactory.create(navigator)
        is PreFlightScreen -> preFlightPresenterFactory.create(navigator, screen)
        is ConferenceScreen -> conferenceScreenFactory.create(navigator, screen)
        is SettingsScreen -> settingsScreenFactory.create(navigator)
        is MediaStatisticsScreen -> mediaStatisticsScreenFactory.create(navigator)
        is RosterScreen -> rosterScreenFactory.create(navigator)
        is ChatScreen -> chatScreenFactory.create(navigator)
        else -> null
    }
}
