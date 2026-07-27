package com.pexip.pulse.sample.screen.conference.annotation

/**
 * UI state for the annotation overlay, produced by [AnnotationPresenter].
 */
data class AnnotationState(
    val isActive: Boolean,
    val isPaintActive: Boolean,
    val canvasWidth: Int,
    val canvasHeight: Int,
    val onEvent: (AnnotationEvent) -> Unit
)
