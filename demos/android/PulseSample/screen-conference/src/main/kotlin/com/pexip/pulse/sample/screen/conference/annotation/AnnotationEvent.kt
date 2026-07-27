package com.pexip.pulse.sample.screen.conference.annotation

import com.slack.circuit.runtime.CircuitUiEvent

sealed interface AnnotationEvent : CircuitUiEvent {
    data object TogglePaint : AnnotationEvent
    data object Undo : AnnotationEvent
    data object Redo : AnnotationEvent
    data object Clear : AnnotationEvent
    data class StrokeBegin(val color: Int, val thickness: Int, val x: Int, val y: Int) : AnnotationEvent
    data class StrokeAddPoint(val x: Int, val y: Int) : AnnotationEvent
    data object StrokeEnd : AnnotationEvent
}
