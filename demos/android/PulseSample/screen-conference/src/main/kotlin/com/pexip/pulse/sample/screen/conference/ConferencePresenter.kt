package com.pexip.pulse.sample.screen.conference

import android.app.Activity
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.media.projection.MediaProjection
import android.os.IBinder
import android.util.Log
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import com.pexip.pulse.conference.Conference
import com.pexip.pulse.conference.ConferenceStateEvent
import com.pexip.pulse.conference.ConnectionStatus
import com.pexip.pulse.conference.RemoteDisconnectEvent
import com.pexip.pulse.core.Pulse
import com.pexip.pulse.media.AudioSession
import com.pexip.pulse.media.CameraFacing
import com.pexip.pulse.media.MediaProjectionSession
import com.pexip.pulse.media.VideoDimensions
import com.pexip.pulse.media.VideoSession
import com.pexip.pulse.sample.screen.chat.ChatScreen
import com.pexip.pulse.sample.screen.roster.RosterScreen
import com.pexip.pulse.sample.screen.settings.SettingsScreen
import com.pexip.pulse.sample.screen.conference.annotation.AnnotationEvent
import com.pexip.pulse.sample.screen.conference.annotation.AnnotationPresenter
import com.pexip.pulse.sample.util.media.RtmpManager
import com.slack.circuit.retained.collectAsRetainedState
import com.slack.circuit.retained.rememberRetained
import com.slack.circuit.runtime.Navigator
import com.slack.circuit.runtime.presenter.Presenter
import kotlinx.coroutines.launch
import javax.inject.Inject
import javax.inject.Singleton

class ConferencePresenter(
    private val navigator: Navigator,
    private val audio: AudioSession,
    private val video: VideoSession,
    private val projection: MediaProjectionSession,
    private val presentationMixManager: PresentationMixManager,
    private val conference: Conference,
    private val rtmpManager: RtmpManager,
    private val annotationPresenter: AnnotationPresenter
) : Presenter<ConferenceState> {
    @Composable
    override fun present(): ConferenceState {
        val applicationContext = LocalContext.current.applicationContext
        val scope = rememberCoroutineScope()
        val rtmpEnabled by rtmpManager.isRunning.collectAsState()
        var initialized by rememberSaveable { mutableStateOf(false) }
        var conferenceName by rememberSaveable { mutableStateOf("") }
        var connectionStatus by rememberSaveable { mutableStateOf(ConnectionStatus.CONNECTING) }
        val isAudioMuted = audio.isMuted.collectAsState()
        val isVideoMuted = video.isMuted.collectAsState()
        val isScreenSharing = projection.isCapturing.collectAsState()
        val camera = video.currentDevice.collectAsState()
        val isFrontCamera = camera.value?.facing == CameraFacing.FRONT
        var isBlurOn by rememberSaveable { mutableStateOf(false) }
        var isVideoMixActive by rememberSaveable { mutableStateOf(false) }
        val annotationState = annotationPresenter.present(rtmpEnabled)
        val remotePresenter by conference.roster.presenter.collectAsRetainedState()
        val remotePresenting by rememberRetained(remotePresenter) {
            derivedStateOf {
                remotePresenter?.isPresenting == true
            }
        }
        val conferenceServiceConnection = rememberRetained {
            object : ServiceConnection {
                override fun onServiceConnected(name: ComponentName, service: IBinder) {}
                override fun onServiceDisconnected(name: ComponentName) {}
            }
        }
        var mediaProjectionService by rememberRetained { mutableStateOf<ConferenceMediaProjectionService?>(null) }
        var isMediaProjectionServiceBound by rememberRetained { mutableStateOf(false) }
        var pendingMediaProjectionStart by rememberRetained { mutableStateOf<(() -> Unit)?>(null) }
        val mediaProjectionServiceConnection = rememberRetained {
            object : ServiceConnection {
                override fun onServiceConnected(name: ComponentName, service: IBinder) {
                    val binder = service as ConferenceMediaProjectionService.MediaProjectionBinder
                    mediaProjectionService = binder.getService()
                    pendingMediaProjectionStart?.invoke()
                    pendingMediaProjectionStart = null
                }
                override fun onServiceDisconnected(name: ComponentName) {
                    mediaProjectionService = null
                }
            }
        }
        val mediaProjectionLauncher = rememberLauncherForActivityResult(
            ActivityResultContracts.StartActivityForResult()
        ) { result ->
            if (result.resultCode == Activity.RESULT_OK && result.data != null) {
                val startProjection: () -> Unit = {
                    mediaProjectionService?.startForegroundIfNeeded()
                    scope.launch {
                        projection.start(
                            permissionResult = result,
                            targetSize = VideoDimensions.FULL_HD,
                            matchTargetAspectRatio = true,
                            callback = object : MediaProjection.Callback() {
                                override fun onStop() {
                                    scope.launch {
                                        if (isMediaProjectionServiceBound) {
                                            try {
                                                applicationContext.unbindService(mediaProjectionServiceConnection)
                                            } catch (e: IllegalArgumentException) {
                                                Log.w("PulseSampleApp", "Service already unbound", e)
                                            }
                                            isMediaProjectionServiceBound = false
                                            mediaProjectionService = null
                                        }
                                    }
                                }
                            }
                        )
                    }
                }

                if (mediaProjectionService != null) {
                    startProjection()
                } else {
                    pendingMediaProjectionStart = startProjection
                }
            }
        }

        fun startScreenSharing() {
            if (!isMediaProjectionServiceBound) {
                applicationContext.bindService(
                    Intent(
                        applicationContext,
                        ConferenceMediaProjectionService::class.java
                    ),
                    mediaProjectionServiceConnection,
                    Context.BIND_AUTO_CREATE
                )
                isMediaProjectionServiceBound = true
            }
            mediaProjectionLauncher.launch(
                projection.createScreenCaptureIntent()
            )
        }

        suspend fun stopScreenSharing() {
            presentationMixManager.releasePresentationInput()
            projection.stop()
            if (isMediaProjectionServiceBound) {
                try {
                    applicationContext.unbindService(mediaProjectionServiceConnection)
                } catch (e: IllegalArgumentException) {
                    Log.w("PulseSampleApp", "Media projection service already unbound", e)
                }
                isMediaProjectionServiceBound = false
                mediaProjectionService = null
            }
        }

        suspend fun stopVideoMix() {
            presentationMixManager.stop(isScreenSharing = isScreenSharing.value)
            isVideoMixActive = presentationMixManager.isActive
        }

        fun disconnect() {
            Log.d("PulseSampleApp", "Disconnecting")
            connectionStatus = ConnectionStatus.DISCONNECTING
            try {
                applicationContext.unbindService(conferenceServiceConnection)
            } catch (e: IllegalArgumentException) {
                Log.w("PulseSampleApp", "Conference service already unbound", e)
            }
            scope.launch {
                annotationPresenter.release()
                stopVideoMix()
                stopScreenSharing()
                video.surfaces.clearAll()
                conference.disconnect()
                audio.disconnect()
                video.disconnect()
                navigator.pop()
                navigator.pop()
            }
        }

        LaunchedEffect(Unit) {
            if (initialized) return@LaunchedEffect
            initialized = true

            applicationContext.bindService(
                Intent(
                    applicationContext,
                    ConferenceService::class.java
                ),
                conferenceServiceConnection,
                Context.BIND_AUTO_CREATE
            )

            if (conference.isConnected()) {
                val info = conference.session.getConferenceInfo()
                conferenceName = info.name
                connectionStatus = ConnectionStatus.CONNECTED
            }

            conference.events.collect { event ->
                Log.d("PulseSampleApp", "Received conference event: $event")
                when (event) {
                    is ConferenceStateEvent -> {
                        connectionStatus = event.status
                    }
                    is RemoteDisconnectEvent -> {
                        disconnect()
                    }
                    else -> {}
                }
            }
        }

        // When the media projection data session input is reconnected (e.g. after
        // a rotation resize), re-acquire the presentation mix input and reconnect
        // the video mix session so the compositor picks up the new input.
        LaunchedEffect(Unit) {
            projection.inputReconnected.collect {
                presentationMixManager.refreshAfterInputReconnect()
            }
        }

        BackHandler {}

        return ConferenceState(
            conferenceName = conferenceName,
            rtmpEnabled = rtmpEnabled,
            rtmpDisplayUrl = rtmpManager.displayUrl,
            isAudioMuted = isAudioMuted.value,
            isVideoMuted = isVideoMuted.value,
            isScreenSharing = isScreenSharing.value,
            remotePresenting = remotePresenting,
            mirrorSelfView = if (rtmpEnabled) false else isFrontCamera,
            isBlurOn = isBlurOn,
            isVideoMixActive = isVideoMixActive,
            annotationState = annotationState,
            loading = connectionStatus != ConnectionStatus.CONNECTED,
            onSelfVideoSurface = { surface ->
                video.surfaces.setSelfVideoSurface(surface)
            },
            onRemoteVideoSurface = { surface ->
                video.surfaces.setRemoteVideoSurface(surface)
            },
            onPresentationVideoSurface = { surface ->
                video.surfaces.setPresentationVideoSurface(surface)
            },
            onEvent = { event ->
                when (event) {
                    ConferenceEvent.MuteAudio -> {
                        scope.launch {
                            audio.toggleMute()
                        }
                    }

                    ConferenceEvent.MuteVideo -> {
                        scope.launch {
                            video.toggleMute()
                        }
                    }

                    ConferenceEvent.ToggleScreenSharing -> {
                        scope.launch {
                            if (isScreenSharing.value) {
                                stopScreenSharing()
                            } else {
                                startScreenSharing()
                            }
                        }
                    }

                    ConferenceEvent.FlipCamera -> {
                        scope.launch {
                            video.flipCamera()
                        }
                    }

                    ConferenceEvent.ToggleBlur -> {
                        isBlurOn = !isBlurOn
                        scope.launch {
                            video.processing.setBackgroundBlur(isBlurOn)
                        }
                    }

                    ConferenceEvent.ToggleVideoMix -> {
                        scope.launch {
                            if (isVideoMixActive) {
                                stopVideoMix()
                            } else {
                                if (!isScreenSharing.value) {
                                    Log.w("PulseSampleApp", "Screen sharing must be active to use video mix")
                                    return@launch
                                }
                                presentationMixManager.start()
                                isVideoMixActive = presentationMixManager.isActive
                            }
                        }
                    }

                    ConferenceEvent.TogglePaint -> {
                        annotationState.onEvent(AnnotationEvent.TogglePaint)
                    }

                    ConferenceEvent.Chat -> {
                        navigator.goTo(ChatScreen)
                    }

                    ConferenceEvent.Roster -> {
                        navigator.goTo(RosterScreen)
                    }

                    ConferenceEvent.Settings -> {
                        navigator.goTo(SettingsScreen)
                    }

                    ConferenceEvent.Disconnect -> {
                        disconnect()
                    }
                }
            }
        )
    }

    @Singleton
    class Factory @Inject constructor(
        private val pulse: Pulse,
        private val rtmpManager: RtmpManager
    ) {
        fun create(navigator: Navigator, screen: ConferenceScreen): Presenter<ConferenceState> =
            ConferencePresenter(
                navigator = navigator,
                audio = pulse.media.audio,
                video = pulse.media.video,
                projection = pulse.media.projection,
                presentationMixManager = PresentationMixManager(
                    videoMix = pulse.media.videoMix,
                    video = pulse.media.video,
                    projection = pulse.media.projection,
                ),
                conference = pulse.conference,
                rtmpManager = rtmpManager,
                annotationPresenter = AnnotationPresenter(pulse, rtmpManager),
            )
    }
}
