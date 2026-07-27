package com.pexip.pulse.sample.screen.home

import android.Manifest
import android.content.Context
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.pexip.pulse.sample.screen.preflight.PreFlightScreen
import com.pexip.pulse.sample.util.media.RtmpManager
import com.slack.circuit.runtime.Navigator
import com.slack.circuit.runtime.presenter.Presenter
import javax.inject.Inject
import javax.inject.Singleton
import kotlinx.coroutines.launch
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.first

class HomePresenter(
    private val navigator: Navigator,
    private val rtmpManager: RtmpManager
) : Presenter<HomeState> {
    @Composable
    override fun present(): HomeState {
        var displayName by rememberSaveable { mutableStateOf("") }
        var conferenceAlias by rememberSaveable { mutableStateOf("") }
        var serverAddress by rememberSaveable { mutableStateOf("") }
        var pin by rememberSaveable { mutableStateOf("") }
        val rtmpEnabled by rtmpManager.isRunning.collectAsState()
        val context = LocalContext.current
        val owner = LocalLifecycleOwner.current
        val checker = remember(context, owner) {
            PermissionChecker(
                context,
                owner
            )
        }
        val scope = rememberCoroutineScope()

        LaunchedEffect(Unit) {
            displayName = context.dataStore.data.map { it[DISPLAY_NAME_KEY] ?: "" }.first()
            conferenceAlias = context.dataStore.data.map { it[CONFERENCE_ALIAS] ?: "" }.first()
            serverAddress = context.dataStore.data.map { it[SERVER_ADDRESS] ?: "" }.first()
        }

        return HomeState(
            displayName = displayName,
            conferenceAlias = conferenceAlias,
            serverAddress = serverAddress,
            pin = pin,
            rtmpEnabled = rtmpEnabled,
            onEvent = { event ->
                when (event) {
                    is HomeEvent.DisplayNameChanged -> displayName = event.value
                    is HomeEvent.ConferenceAliasChanged -> conferenceAlias = event.value
                    is HomeEvent.ServerAddressChanged -> serverAddress = event.value
                    is HomeEvent.PinChanged -> pin = event.value
                    is HomeEvent.ToggleRtmp -> {
                        scope.launch {
                            try {
                                if (rtmpEnabled) {
                                    rtmpManager.stopServer()
                                } else {
                                    rtmpManager.startServer()
                                }
                            } catch (_: Exception) { }
                        }
                    }
                    is HomeEvent.JoinConference -> {
                        scope.launch {
                            context.dataStore.edit { it[DISPLAY_NAME_KEY] = displayName }
                            context.dataStore.edit { it[CONFERENCE_ALIAS] = conferenceAlias }
                            context.dataStore.edit { it[SERVER_ADDRESS] = serverAddress }

                            checker.awaitPermission(Manifest.permission.CAMERA)
                            checker.awaitPermission(Manifest.permission.RECORD_AUDIO)

                            navigator.goTo(
                                PreFlightScreen(
                                    displayName = displayName,
                                    conferenceAlias = conferenceAlias,
                                    serverAddress,
                                    pin = pin
                                )
                            )
                        }
                    }
                }
            }
        )
    }

    @Singleton
    class Factory @Inject constructor(private val rtmpManager: RtmpManager) {
        fun create(navigator: Navigator): Presenter<HomeState> = HomePresenter(navigator, rtmpManager)
    }

    companion object {
        val DISPLAY_NAME_KEY = stringPreferencesKey("display_name")
        val CONFERENCE_ALIAS = stringPreferencesKey("conference_alias")
        val SERVER_ADDRESS = stringPreferencesKey("server_address")
    }
}

private val Context.dataStore by preferencesDataStore(name = "user_prefs")
