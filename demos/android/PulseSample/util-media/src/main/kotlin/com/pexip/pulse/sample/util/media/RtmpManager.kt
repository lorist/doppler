package com.pexip.pulse.sample.util.media

import android.util.Log
import com.pexip.pulse.core.Pulse
import com.pexip.pulse.media.MediaContent
import com.pexip.pulse.media.VideoMixConfig
import com.pexip.pulse.media.VideoMixInputId
import com.pexip.pulse.media.VideoMixLayer
import com.pexip.pulse.media.VideoProcessTypeMask
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import java.net.NetworkInterface
import javax.inject.Inject
import javax.inject.Singleton

private const val RTMP_PORT = 1935
private const val RTMP_PATH = "live"

/**
 * Manages the RTMP server lifecycle and video-mix composition.
 *
 * Lifecycle is split into two phases:
 * 1. [startServer] / [stopServer] – controls the RTMP listener
 * 2. [setupVideoMix] / [teardownVideoMix] – sets up the video mixer with camera
 */
@Singleton
class RtmpManager @Inject constructor(private val pulse: Pulse) {

    private val rtmp get() = pulse.media.rtmp
    private val videoMix get() = pulse.media.videoMix
    private val video get() = pulse.media.video

    private val rtmpInputMediaContent = MediaContent.PRESENTATION
    private val mixMediaContent = MediaContent.MAIN

    private val scope = CoroutineScope(Dispatchers.Default)
    private var publisherObserverJob: Job? = null

    private val _isRunning = MutableStateFlow(false)

    /** Whether the RTMP server is currently active. */
    val isRunning: StateFlow<Boolean> = _isRunning.asStateFlow()

    /** The RTMP URL that external publishers should stream to. Set after [startServer]. */
    var displayUrl: String = ""
        private set

    private var rtmpMixInputId: VideoMixInputId = VideoMixInputId.NONE
    private var cameraMixInputId: VideoMixInputId = VideoMixInputId.NONE
    private var annotationMixInputId: VideoMixInputId = VideoMixInputId.NONE
    private var rtmpInputConnected: Boolean = false
    private var isVideoMixActive: Boolean = false
    private var isVideoMixSetup: Boolean = false

    /**
     * Starts the RTMP listener. Called from the Home screen when the checkbox
     * is toggled on. Does NOT set up the video mixer (camera not yet available).
     */
    suspend fun startServer() {
        if (_isRunning.value) return
        try {
            rtmp.connectInput(
                path = RTMP_PATH,
                listeningPort = RTMP_PORT,
                supportAudio = true,
                supportVideo = true,
                mediaContent = rtmpInputMediaContent
            )
            rtmpInputConnected = true
            displayUrl = buildRtmpUrl()
            _isRunning.value = true
        } catch (e: Exception) {
            Log.e(TAG, "Failed to start RTMP server", e)
            rtmpInputConnected = false
            displayUrl = ""
            throw e
        }
    }

    /**
     * Stops the RTMP listener and tears down the video mix if active.
     * Called from the Home screen when the checkbox is toggled off.
     */
    suspend fun stopServer() {
        teardownVideoMix()
        if (rtmpInputConnected) {
            try {
                rtmp.disconnectInput(rtmpInputMediaContent)
            } catch (e: Exception) {
                Log.w(TAG, "Failed to disconnect RTMP input", e)
            }
            rtmpInputConnected = false
        }
        _isRunning.value = false
        displayUrl = ""
    }

    /**
     * Sets up the video mixer with RTMP + camera inputs and starts observing
     * publisher connections. Called from PreFlight after camera is connected.
     */
    suspend fun setupVideoMix() {
        if (!_isRunning.value) return
        teardownVideoMix()
        try {
            rtmpMixInputId = videoMix.inputFromRtmpSession(rtmpInputMediaContent)

            val currentDevice = video.currentDevice.value
            cameraMixInputId = if (currentDevice != null) {
                videoMix.inputFromDevice(currentDevice)
            } else {
                VideoMixInputId.NONE
            }

            connectVideoMixCameraOnly()
            isVideoMixSetup = true
            startPublisherObserver()
        } catch (e: Exception) {
            Log.e(TAG, "Failed to setup video mix", e)
            teardownVideoMix()
            throw e
        }
    }

    /**
     * Set (or clear) an annotation overlay input and rebuild the video mix.
     */
    suspend fun setAnnotationOverlay(inputId: VideoMixInputId) {
        annotationMixInputId = inputId
        if (isVideoMixActive) {
            rebuildCurrentMix()
        }
    }

    /**
     * Tears down the video mixer. Called when navigating back from PreFlight
     * or when stopping the server.
     */
    suspend fun teardownVideoMix() {
        publisherObserverJob?.cancel()
        publisherObserverJob = null
        if (isVideoMixActive) {
            try {
                videoMix.disconnect(mixMediaContent)
            } catch (e: Exception) {
                Log.w(TAG, "Failed to disconnect video mix", e)
            }
            isVideoMixActive = false
        }
        if (cameraMixInputId != VideoMixInputId.NONE) {
            try {
                videoMix.releaseInput(cameraMixInputId)
            } catch (e: Exception) {
                Log.w(TAG, "Failed to release camera mix input", e)
            }
            cameraMixInputId = VideoMixInputId.NONE
        }
        if (rtmpMixInputId != VideoMixInputId.NONE) {
            try {
                videoMix.releaseInput(rtmpMixInputId)
            } catch (e: Exception) {
                Log.w(TAG, "Failed to release RTMP mix input", e)
            }
            rtmpMixInputId = VideoMixInputId.NONE
        }
        isVideoMixSetup = false
    }

    private fun startPublisherObserver() {
        publisherObserverJob = scope.launch {
            rtmp.publisherConnected.collect { connected ->
                if (connected) {
                    onPublisherConnected()
                } else if (isVideoMixActive) {
                    onPublisherDisconnected()
                }
            }
        }
    }

    private fun cameraOverlayLayer(layer: Int = 0) = VideoMixLayer(
        inputId = cameraMixInputId,
        layer = layer,
        widthRatio = 0.25,
        heightRatio = 0.25,
        xCentrepoint = 1.0,
        yCentrepoint = 1.0,
        videoprocMask = VideoProcessTypeMask.SEGMENTATION
    )

    private fun annotationOverlayLayer(layer: Int) = VideoMixLayer(
        inputId = annotationMixInputId,
        layer = layer,
        widthRatio = 1.0,
        heightRatio = 1.0,
        xCentrepoint = 0.5,
        yCentrepoint = 0.5,
        videoprocMask = VideoProcessTypeMask.NONE
    )

    private var hasPublisher: Boolean = false

    private suspend fun onPublisherConnected() {
        if (!rtmpInputConnected) return
        hasPublisher = true
        try {
            if (isVideoMixActive) {
                videoMix.disconnect(mixMediaContent)
                isVideoMixActive = false
            }
            val layers = buildPublisherMixLayers()
            videoMix.connect(
                config = VideoMixConfig(layers),
                mediaContent = mixMediaContent
            )
            isVideoMixActive = true
        } catch (e: Exception) {
            Log.e(TAG, "Failed to connect video mix with RTMP", e)
        }
    }

    private suspend fun onPublisherDisconnected() {
        if (!isVideoMixActive) return
        hasPublisher = false
        try {
            videoMix.disconnect(mixMediaContent)
            isVideoMixActive = false
            connectVideoMixCameraOnly()
        } catch (e: Exception) {
            Log.e(TAG, "Failed to revert video mix to camera-only", e)
        }
    }

    private suspend fun connectVideoMixCameraOnly() {
        if (cameraMixInputId == VideoMixInputId.NONE) return
        val layers = mutableListOf(cameraOverlayLayer(layer = 0))
        if (annotationMixInputId != VideoMixInputId.NONE) {
            layers.add(annotationOverlayLayer(layer = 1))
        }
        videoMix.connect(
            config = VideoMixConfig(layers),
            mediaContent = mixMediaContent
        )
        isVideoMixActive = true
    }

    private fun buildPublisherMixLayers(): List<VideoMixLayer> {
        val layers = mutableListOf(
            VideoMixLayer(
                inputId = rtmpMixInputId,
                layer = 0,
                widthRatio = 1.0,
                heightRatio = 1.0,
                xCentrepoint = 0.5,
                yCentrepoint = 0.5,
                videoprocMask = VideoProcessTypeMask.NONE
            )
        )
        if (cameraMixInputId != VideoMixInputId.NONE) {
            layers.add(cameraOverlayLayer(layer = 1))
        }
        if (annotationMixInputId != VideoMixInputId.NONE) {
            layers.add(annotationOverlayLayer(layer = 2))
        }
        return layers
    }

    private suspend fun rebuildCurrentMix() {
        if (!isVideoMixActive) return
        try {
            videoMix.disconnect(mixMediaContent)
            isVideoMixActive = false
            if (hasPublisher) {
                val layers = buildPublisherMixLayers()
                videoMix.connect(
                    config = VideoMixConfig(layers),
                    mediaContent = mixMediaContent
                )
                isVideoMixActive = true
            } else {
                connectVideoMixCameraOnly()
            }
        } catch (e: Exception) {
            Log.e(TAG, "Failed to rebuild video mix", e)
        }
    }

    companion object {
        private const val TAG = "PulseSampleApp"
    }
}

private fun buildRtmpUrl(): String {
    val ip = getDeviceIpAddress()
    return "rtmp://$ip:$RTMP_PORT/app/$RTMP_PATH"
}

private fun getDeviceIpAddress(): String {
    try {
        val interfaces = NetworkInterface.getNetworkInterfaces() ?: return "unknown"
        for (intf in interfaces) {
            if (intf.isLoopback || !intf.isUp) continue
            for (addr in intf.inetAddresses) {
                if (addr.isLoopbackAddress) continue
                val hostAddress = addr.hostAddress ?: continue
                if (!hostAddress.contains(':')) {
                    return hostAddress
                }
            }
        }
    } catch (e: Exception) {
        Log.w("PulseSampleApp", "Failed to get device IP address", e)
    }
    return "unknown"
}
