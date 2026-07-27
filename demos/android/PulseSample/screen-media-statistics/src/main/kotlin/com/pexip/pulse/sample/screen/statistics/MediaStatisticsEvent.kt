package com.pexip.pulse.sample.screen.statistics

import com.slack.circuit.runtime.CircuitUiEvent

sealed interface MediaStatisticsEvent : CircuitUiEvent {
    data object Back : MediaStatisticsEvent
}
