package com.pexip.pulse.sample.screen.preflight

import android.net.Uri
import androidx.activity.compose.BackHandler
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.core.net.toUri
import com.pexip.pulse.conference.Conference
import com.pexip.pulse.conference.ConnectResult
import com.pexip.pulse.conference.ConnectionRequest
import com.pexip.pulse.conference.SsoProvider
import com.pexip.pulse.core.Pulse
import com.pexip.pulse.media.AudioSession
import com.pexip.pulse.media.CameraFacing
import com.pexip.pulse.media.VideoSession
import com.pexip.pulse.sample.screen.conference.ConferenceScreen
import com.pexip.pulse.sample.screen.settings.SettingsScreen
import com.pexip.pulse.sample.util.media.RtmpManager
import com.slack.circuit.runtime.Navigator
import com.slack.circuit.runtime.presenter.Presenter
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import javax.inject.Inject
import javax.inject.Singleton

class PreFlightPresenter(
    private val screen: PreFlightScreen,
    private val navigator: Navigator,
    private val audio: AudioSession,
    private val video: VideoSession,
    private val rtmpManager: RtmpManager,
    private val conference: Conference
) : Presenter<PreFlightState> {

    @Composable
    override fun present(): PreFlightState {
        val scope = rememberCoroutineScope()
        var initialized by rememberSaveable { mutableStateOf(false) }
        var connecting by rememberSaveable { mutableStateOf(false) }
        var ssoRedirectUrl by rememberSaveable { mutableStateOf<Uri?>(null) }
        var ssoProviders by remember { mutableStateOf<List<SsoProvider>>(emptyList()) }
        var ssoProviderSelected by remember { mutableStateOf<SsoProvider?>(null) }
        var errorMessage by remember { mutableStateOf<String?>(null) }
        val isAudioMuted = audio.isMuted.collectAsState()
        val isVideoMuted = video.isMuted.collectAsState()
        val camera = video.currentDevice.collectAsState()
        val isFrontCamera = camera.value?.facing == CameraFacing.FRONT
        var isBlurOn by rememberSaveable { mutableStateOf(false) }

        val rtmpEnabled by rtmpManager.isRunning.collectAsState()
        val rtmpDisplayUrl = rtmpManager.displayUrl

        LaunchedEffect(Unit) {
            if (initialized) return@LaunchedEffect
            initialized = true

            scope.launch {
                audio.connect()
                audio.mute(false)
                video.connect(cameraFacing = CameraFacing.FRONT)
                video.mute(false)

                if (rtmpEnabled) {
                    try {
                        rtmpManager.setupVideoMix()
                    } catch (e: Exception) {
                        errorMessage = "Video mix setup failed: ${e.message}"
                    }
                }
            }
        }

        BackHandler {
            CoroutineScope(Dispatchers.Default).launch {
                if (connecting) {
                    conference.cancelRequest()
                }
                navigateBack()
            }
        }

        fun connect(ssoDataUrl: Uri? = null) {
            var request = ConnectionRequest(
                conferenceName = screen.conferenceAlias,
                serverAddress = screen.serverAddress,
                displayName = screen.displayName,
                pinCode = screen.pin,
                ssoProvider = ssoProviderSelected
            )
            ssoDataUrl?.let { request = request.withSsoToken(it) }
            ssoProviders = emptyList()
            ssoRedirectUrl = null
            errorMessage = null
            connecting = true
            scope.launch {
                try {
                    when (val result = conference.connect(request)) {
                        is ConnectResult.Success -> navigator.goTo(ConferenceScreen)
                        is ConnectResult.SsoRequired -> ssoProviders = result.providers
                        is ConnectResult.SsoRedirect -> ssoRedirectUrl = result.request.url.toUri()
                        is ConnectResult.VersionNotAllowed -> errorMessage =
                            "Server version ${result.version.string} is not allowed."
                        is ConnectResult.VersionUnsupported -> errorMessage =
                            "Server version ${result.version.string} is not supported."
                        is ConnectResult.Failed.PulseFailure -> errorMessage =
                            result.message ?: result.error.name
                        is ConnectResult.Failed.Unexpected -> errorMessage =
                            result.exception.message ?: "Connection failed"
                    }
                } catch (e: Exception) {
                    errorMessage = e.message
                } finally {
                    connecting = false
                }
            }
        }

        return PreFlightState(
            displayName = screen.displayName,
            conferenceAlias = screen.conferenceAlias,
            serverAddress = screen.serverAddress,
            pin = screen.pin,
            rtmpEnabled = rtmpEnabled,
            rtmpDisplayUrl = rtmpDisplayUrl,
            mirrorSelfView = if (rtmpEnabled) false else isFrontCamera,
            isAudioMuted = isAudioMuted.value,
            isVideoMuted = isVideoMuted.value,
            isBlurOn = isBlurOn,
            connecting = connecting,
            ssoProviders = ssoProviders,
            ssoRedirectUrl = ssoRedirectUrl,
            errorMessage = errorMessage,
            onSelfVideoSurface = { surface ->
                video.surfaces.setSelfVideoSurface(surface)
            },
            onEvent = { event ->
                when (event) {
                    PreFlightEvent.MuteAudio -> {
                        scope.launch {
                            audio.toggleMute()
                        }
                    }

                    PreFlightEvent.MuteVideo -> {
                        scope.launch {
                            video.toggleMute()
                        }
                    }

                    PreFlightEvent.FlipCamera -> {
                        scope.launch {
                            video.flipCamera()
                        }
                    }

                    PreFlightEvent.ToggleBlur -> {
                        isBlurOn = !isBlurOn
                        scope.launch {
                            video.processing.setBackgroundBlur(isBlurOn)
                        }
                    }

                    PreFlightEvent.DismissErrorAlert -> {
                        errorMessage = null
                    }

                    PreFlightEvent.DismissSsoProviderPicker -> {
                        ssoProviders = emptyList()
                        connecting = false
                    }

                    is PreFlightEvent.SelectSsoProvider -> {
                        ssoProviderSelected = event.provider
                        connect()
                    }

                    is PreFlightEvent.HandleSsoData -> {
                        connect(ssoDataUrl = event.data)
                    }

                    PreFlightEvent.Join -> {
                        connect()
                    }

                    PreFlightEvent.Cancel -> {
                        ssoProviderSelected = null
                        ssoRedirectUrl = null
                        scope.launch {
                            conference.cancelRequest()
                        }
                    }

                    PreFlightEvent.Back -> {
                        scope.launch {
                            navigateBack()
                        }
                    }

                    PreFlightEvent.Settings -> {
                        navigator.goTo(SettingsScreen)
                    }
                }
            }
        )
    }

    private suspend fun navigateBack() {
        rtmpManager.teardownVideoMix()
        audio.disconnect()
        video.disconnect()
        navigator.pop()
    }

    @Singleton
    class Factory @Inject constructor(
        private val pulse: Pulse,
        private val rtmpManager: RtmpManager
    ) {
        fun create(navigator: Navigator, screen: PreFlightScreen): Presenter<PreFlightState> =
            PreFlightPresenter(
                screen = screen,
                navigator = navigator,
                audio = pulse.media.audio,
                video = pulse.media.video,
                rtmpManager = rtmpManager,
                conference = pulse.conference
            )
    }
}
