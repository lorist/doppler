package com.pexip.pulse.sample.screen.settings

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import com.pexip.pulse.core.Pulse
import com.pexip.pulse.media.MediaSession
import com.pexip.pulse.sample.screen.statistics.MediaStatisticsScreen
import com.pexip.pulse.sample.util.media.RtmpManager
import com.pexip.sdk.media.AudioDeviceManager
import com.pexip.sdk.media.coroutines.availableAudioDevicesIn
import com.pexip.sdk.media.coroutines.selectedAudioDeviceIn
import com.slack.circuit.runtime.Navigator
import com.slack.circuit.runtime.presenter.Presenter
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.launch
import javax.inject.Inject
import javax.inject.Singleton

class SettingsPresenter(
    private val navigator: Navigator,
    private val media: MediaSession,
    private val audioDeviceManager: AudioDeviceManager,
    private val rtmpManager: RtmpManager
) : Presenter<SettingsState> {

    @Composable
    override fun present(): SettingsState {
        val scope = rememberCoroutineScope()
        var initialized by rememberSaveable { mutableStateOf(false) }
        val audioDevices = audioDeviceManager
            .availableAudioDevicesIn(scope, SharingStarted.Eagerly)
            .collectAsState()
        val currentAudioDevice = audioDeviceManager
            .selectedAudioDeviceIn(scope, SharingStarted.Eagerly)
            .collectAsState()
        val videoDevices = media.video.devices.collectAsState()
        val currentVideoDevice = media.video.currentDevice.collectAsState()
        val rtmpEnabled by rtmpManager.isRunning.collectAsState()

        LaunchedEffect(Unit) {
            if (initialized) return@LaunchedEffect
            initialized = true
        }

        return SettingsState(
            audioDevices = audioDevices.value,
            currentAudioDevice = currentAudioDevice.value,
            videoDevices = videoDevices.value,
            currentVideoDevice = currentVideoDevice.value,
            rtmpEnabled = rtmpEnabled,
            onEvent = { event ->
                when (event) {
                    is SettingsEvent.AudioDeviceSelected -> {
                        scope.launch {
                            if (event.device != null) {
                                audioDeviceManager.selectAudioDevice(event.device)
                                media.audio.connect()
                            } else {
                                audioDeviceManager.clearAudioDevice()
                                media.audio.disconnect()
                            }
                        }
                    }

                    is SettingsEvent.VideoDeviceSelected -> {
                        scope.launch {
                            if (event.device != null) {
                                media.video.connect(device = event.device)
                            } else {
                                media.video.disconnect()
                            }
                        }
                    }

                    is SettingsEvent.MediaStatistics -> {
                        navigator.goTo(MediaStatisticsScreen)
                    }

                    SettingsEvent.Back -> {
                        navigator.pop()
                    }
                }
            }
        )
    }

    @Singleton
    class Factory @Inject constructor(
        private val pulse: Pulse,
        private val audioDeviceManager: AudioDeviceManager,
        private val rtmpManager: RtmpManager
    ) {
        fun create(
            navigator: Navigator,
        ): Presenter<SettingsState> = SettingsPresenter(
            navigator = navigator,
            media = pulse.media,
            audioDeviceManager = audioDeviceManager,
            rtmpManager = rtmpManager
        )
    }
}
