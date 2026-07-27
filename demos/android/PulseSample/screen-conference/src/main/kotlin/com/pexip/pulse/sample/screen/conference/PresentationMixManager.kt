package com.pexip.pulse.sample.screen.conference

import android.util.Log
import com.pexip.pulse.media.MediaContent
import com.pexip.pulse.media.MediaProjectionSession
import com.pexip.pulse.media.VideoMixConfig
import com.pexip.pulse.media.VideoMixInputId
import com.pexip.pulse.media.VideoMixLayer
import com.pexip.pulse.media.VideoMixSession
import com.pexip.pulse.media.VideoProcessTypeMask
import com.pexip.pulse.media.VideoSession

class PresentationMixManager(
    private val videoMix: VideoMixSession,
    private val video: VideoSession,
    private val projection: MediaProjectionSession,
) {
    var isActive: Boolean = false
        private set

    private var cameraInputId: VideoMixInputId = VideoMixInputId.NONE
    private var presentationInputId: VideoMixInputId = VideoMixInputId.NONE

    fun buildConfig(
        presentationInputId: VideoMixInputId,
        cameraInputId: VideoMixInputId,
    ): VideoMixConfig {
        val layers = mutableListOf(
            VideoMixLayer(
                inputId = presentationInputId,
                layer = 0,
                widthRatio = 1.0,
                heightRatio = 1.0,
                xCentrepoint = 0.5,
                yCentrepoint = 0.5,
                videoprocMask = VideoProcessTypeMask.NONE
            )
        )
        if (cameraInputId != VideoMixInputId.NONE) {
            layers.add(
                VideoMixLayer(
                    inputId = cameraInputId,
                    layer = 1,
                    widthRatio = 0.25,
                    heightRatio = 0.25,
                    xCentrepoint = 1.0,
                    yCentrepoint = 1.0,
                    videoprocMask = VideoProcessTypeMask.SEGMENTATION
                )
            )
        }
        return VideoMixConfig(layers)
    }

    suspend fun start() {
        try {
            presentationInputId = videoMix.inputFromDataSession(MediaContent.PRESENTATION)

            val currentDevice = video.currentDevice.value
            var cameraInputId = VideoMixInputId.NONE
            if (currentDevice != null) {
                cameraInputId = videoMix.inputFromDevice(currentDevice)
                this.cameraInputId = cameraInputId
            }

            videoMix.connect(
                config = buildConfig(presentationInputId, cameraInputId),
                mediaContent = MediaContent.PRESENTATION
            )
            isActive = true
        } catch (e: Exception) {
            Log.e("PulseSampleApp", "Failed to start video mix", e)
            stop(isScreenSharing = false)
        }
    }

    suspend fun stop(isScreenSharing: Boolean) {
        if (!isActive) return
        try {
            videoMix.disconnect(MediaContent.PRESENTATION)
        } catch (e: Exception) {
            Log.w("PulseSampleApp", "Failed to disconnect video mix", e)
        }
        if (cameraInputId != VideoMixInputId.NONE) {
            try {
                videoMix.releaseInput(cameraInputId)
            } catch (e: Exception) {
                Log.w("PulseSampleApp", "Failed to release camera input", e)
            }
            cameraInputId = VideoMixInputId.NONE
        }
        if (presentationInputId != VideoMixInputId.NONE) {
            try {
                videoMix.releaseInput(presentationInputId)
            } catch (e: Exception) {
                Log.w("PulseSampleApp", "Failed to release presentation input", e)
            }
            presentationInputId = VideoMixInputId.NONE
        }
        // Reconnect the data session video input so that screen capture frames
        // resume flowing directly to the remote end.
        if (isScreenSharing) {
            try {
                projection.reconnectInput()
            } catch (e: Exception) {
                Log.w("PulseSampleApp", "Failed to reconnect projection input after video mix stop", e)
            }
        }
        isActive = false
    }

    suspend fun releasePresentationInput() {
        if (presentationInputId != VideoMixInputId.NONE) {
            try {
                videoMix.releaseInput(presentationInputId)
            } catch (e: Exception) {
                Log.w("PulseSampleApp", "Failed to release presentation input on share stop", e)
            }
            presentationInputId = VideoMixInputId.NONE
        }
    }

    suspend fun refreshAfterInputReconnect() {
        if (!isActive) return
        Log.d("PulseSampleApp", "Data session input reconnected — refreshing video mix")
        try {
            if (presentationInputId != VideoMixInputId.NONE) {
                videoMix.releaseInput(presentationInputId)
            }
            presentationInputId = videoMix.inputFromDataSession(MediaContent.PRESENTATION)

            videoMix.disconnect(MediaContent.PRESENTATION)
            videoMix.connect(
                config = buildConfig(presentationInputId, cameraInputId),
                mediaContent = MediaContent.PRESENTATION
            )
        } catch (e: Exception) {
            Log.e("PulseSampleApp", "Failed to refresh video mix after input reconnect", e)
        }
    }
}
