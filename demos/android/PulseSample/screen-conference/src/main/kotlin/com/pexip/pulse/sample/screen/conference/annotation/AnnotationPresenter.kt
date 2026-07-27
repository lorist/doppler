package com.pexip.pulse.sample.screen.conference.annotation

import android.util.Log
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import com.pexip.pulse.core.Pulse
import com.pexip.pulse.media.AnnotationStrokeId
import com.pexip.pulse.media.VideoMixInputId
import com.pexip.pulse.sample.util.media.RtmpManager
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.cancel
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * Default annotation canvas dimensions (matches typical 720p layout).
 */
private const val ANNOTATION_WIDTH = 1280
private const val ANNOTATION_HEIGHT = 720

private const val TAG = "PulseSampleApp"

/**
 * Nested presenter that manages the full annotation lifecycle: acquiring/releasing
 * the video mix overlay input, drawing strokes, and handling undo/redo/clear.
 */
class AnnotationPresenter(
    private val pulse: Pulse,
    private val rtmpManager: RtmpManager
) {
    val canvasWidth: Int = ANNOTATION_WIDTH
    val canvasHeight: Int = ANNOTATION_HEIGHT

    private val videoMix get() = pulse.media.videoMix
    private val annotation get() = pulse.media.annotation
    private var scope = CoroutineScope(Dispatchers.Default + Job())
    private val isActive = MutableStateFlow(false)
    private var annotationInputId: VideoMixInputId = VideoMixInputId.NONE
    private val activeStrokeHolder = arrayOf(AnnotationStrokeId.NONE)

    /**
     * Release the annotation overlay. Call before disconnecting.
     */
    suspend fun release() {
        if (annotationInputId == VideoMixInputId.NONE) return
        val id = annotationInputId
        activeStrokeHolder[0] = AnnotationStrokeId.NONE
        scope.cancel()
        scope = CoroutineScope(Dispatchers.Default + Job())
        try {
            rtmpManager.setAnnotationOverlay(VideoMixInputId.NONE)
            videoMix.releaseInput(id)
            annotationInputId = VideoMixInputId.NONE
            isActive.value = false
        } catch (e: Exception) {
            Log.w(TAG, "Failed to release annotation input", e)
        }
    }

    private suspend fun acquire(): VideoMixInputId {
        if (annotationInputId != VideoMixInputId.NONE) return annotationInputId
        try {
            annotationInputId = videoMix.inputFromAnnotation(canvasWidth, canvasHeight)
            isActive.value = true
            rtmpManager.setAnnotationOverlay(annotationInputId)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to acquire annotation input", e)
        }
        return annotationInputId
    }

    @Composable
    fun present(rtmpEnabled: Boolean): AnnotationState {
        val presenterScope = rememberCoroutineScope()
        var isPaintActive by rememberSaveable { mutableStateOf(false) }
        val isAnnotationAcquired by isActive.asStateFlow().collectAsState()

        // Serial channel to ensure stroke operations are processed in order.
        val strokeChannel = remember {
            Channel<suspend () -> Unit>(Channel.UNLIMITED)
        }

        // Active stroke ID tracked within the serial processor (class-level holder).
        val activeStrokeHolder = this.activeStrokeHolder

        // Single coroutine draining stroke operations sequentially.
        remember {
            presenterScope.launch {
                for (op in strokeChannel) {
                    op()
                }
            }
        }

        fun drainPendingStrokes() {
            var drained = strokeChannel.tryReceive()
            while (drained.isSuccess) {
                drained = strokeChannel.tryReceive()
            }
            activeStrokeHolder[0] = AnnotationStrokeId.NONE
        }

        return AnnotationState(
            isActive = rtmpEnabled && isAnnotationAcquired,
            isPaintActive = isPaintActive,
            canvasWidth = canvasWidth,
            canvasHeight = canvasHeight,
            onEvent = { event ->
                when (event) {
                    AnnotationEvent.TogglePaint -> {
                        if (isPaintActive) {
                            isPaintActive = false
                        } else if (annotationInputId == VideoMixInputId.NONE) {
                            presenterScope.launch {
                                val id = acquire()
                                if (id != VideoMixInputId.NONE) {
                                    isPaintActive = true
                                }
                            }
                        } else {
                            isPaintActive = true
                        }
                    }

                    AnnotationEvent.Undo -> {
                        drainPendingStrokes()
                        strokeChannel.trySend {
                            val id = annotationInputId
                            if (id != VideoMixInputId.NONE) {
                                annotation.undo(id)
                                annotation.commit(id)
                            }
                        }
                    }

                    AnnotationEvent.Redo -> {
                        drainPendingStrokes()
                        strokeChannel.trySend {
                            val id = annotationInputId
                            if (id != VideoMixInputId.NONE) {
                                annotation.redo(id)
                                annotation.commit(id)
                            }
                        }
                    }

                    AnnotationEvent.Clear -> {
                        drainPendingStrokes()
                        strokeChannel.trySend {
                            val id = annotationInputId
                            if (id != VideoMixInputId.NONE) {
                                annotation.clear(id)
                                annotation.commit(id)
                            }
                        }
                    }

                    is AnnotationEvent.StrokeBegin -> {
                        strokeChannel.trySend {
                            val id = annotationInputId
                            if (id == VideoMixInputId.NONE) return@trySend
                            try {
                                annotation.setColor(
                                    id,
                                    r = (event.color shr 16) and 0xFF,
                                    g = (event.color shr 8) and 0xFF,
                                    b = event.color and 0xFF,
                                    a = (event.color ushr 24) and 0xFF
                                )
                                annotation.setThickness(id, event.thickness)
                                val strokeId = annotation.strokeBegin(id)
                                activeStrokeHolder[0] = strokeId
                                annotation.strokeAddPoint(id, strokeId, event.x, event.y)
                                annotation.commit(id)
                            } catch (e: Exception) {
                                Log.w(TAG, "strokeBegin failed", e)
                                activeStrokeHolder[0] = AnnotationStrokeId.NONE
                            }
                        }
                    }

                    is AnnotationEvent.StrokeAddPoint -> {
                        strokeChannel.trySend {
                            val id = annotationInputId
                            if (id == VideoMixInputId.NONE) return@trySend
                            val strokeId = activeStrokeHolder[0]
                            if (strokeId != AnnotationStrokeId.NONE) {
                                try {
                                    annotation.strokeAddPoint(id, strokeId, event.x, event.y)
                                    annotation.commit(id)
                                } catch (e: Exception) {
                                    Log.w(TAG, "strokeAddPoint failed, resetting active stroke", e)
                                    activeStrokeHolder[0] = AnnotationStrokeId.NONE
                                }
                            }
                        }
                    }

                    AnnotationEvent.StrokeEnd -> {
                        strokeChannel.trySend {
                            val id = annotationInputId
                            if (id == VideoMixInputId.NONE) return@trySend
                            val strokeId = activeStrokeHolder[0]
                            if (strokeId != AnnotationStrokeId.NONE) {
                                try {
                                    annotation.strokeEnd(id, strokeId)
                                } catch (e: Exception) {
                                    Log.w(TAG, "strokeEnd failed", e)
                                }
                                activeStrokeHolder[0] = AnnotationStrokeId.NONE
                            }
                        }
                    }
                }
            }
        )
    }
}
