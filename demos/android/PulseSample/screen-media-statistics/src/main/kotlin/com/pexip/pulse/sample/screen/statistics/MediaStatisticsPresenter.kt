package com.pexip.pulse.sample.screen.statistics

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import com.pexip.pulse.core.Pulse
import com.pexip.pulse.media.MediaStatisticsManager
import com.slack.circuit.runtime.Navigator
import com.slack.circuit.runtime.presenter.Presenter
import dagger.assisted.Assisted
import dagger.assisted.AssistedFactory
import dagger.assisted.AssistedInject
import kotlinx.collections.immutable.ImmutableList
import kotlinx.collections.immutable.persistentListOf

class MediaStatisticsPresenter internal constructor(
    private val navigator: Navigator,
    private val manager: MediaStatisticsManager,
) : Presenter<MediaStatisticsState> {

    @AssistedInject
    constructor(
        @Assisted navigator: Navigator,
        pulse: Pulse,
    ) : this(
        navigator = navigator,
        manager = pulse.media.mediaStatistics,
    )

    @Composable
    override fun present(): MediaStatisticsState {
        val mediaStatistics by produceMediaStatisticsState()
        return MediaStatisticsState(
            onEvent = {
                when (it) {
                    is MediaStatisticsEvent.Back -> navigator.pop()
                }
            },
            sections = mediaStatistics,
        )
    }

    @Composable
    private fun produceMediaStatisticsState() =
        produceState<ImmutableList<MediaStatisticsSection>>(persistentListOf()) {
            manager.mediaStatistics.collect {
                when (it) {
                    null -> Unit
                    else -> value = persistentListOf(
                        MediaStatisticsSection.Quality(
                            content = MediaStatisticsQualityContent(it),
                        ),
                        MediaStatisticsSection.Audio(
                            content = MediaStatisticsContent(
                                input = it.audioRx,
                                output = it.audioTx,
                            )
                        ),
                        MediaStatisticsSection.Video(
                            content = MediaStatisticsContent(
                                input = it.videoRx,
                                output = it.videoTx,
                            )
                        ),
                        MediaStatisticsSection.Presentation(
                            content = MediaStatisticsContent(
                                input = it.slidesRx,
                                output = it.slidesTx,
                            )
                        )
                    )
                }
            }
        }

    @AssistedFactory
    fun interface Factory {

        fun create(navigator: Navigator): MediaStatisticsPresenter
    }
}
