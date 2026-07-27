package com.pexip.pulse.sample.screen.statistics

import com.slack.circuit.runtime.CircuitUiState
import kotlinx.collections.immutable.ImmutableList

data class MediaStatisticsState(
    val onEvent: (MediaStatisticsEvent) -> Unit,
    val sections: ImmutableList<MediaStatisticsSection>,
) : CircuitUiState
