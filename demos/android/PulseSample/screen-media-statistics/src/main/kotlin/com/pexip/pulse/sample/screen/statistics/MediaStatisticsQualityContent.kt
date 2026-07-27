package com.pexip.pulse.sample.screen.statistics

import com.pexip.pulse.media.MediaStats
import kotlin.math.max

data class MediaStatisticsQualityContent(
    val packetsLost: MediaStatisticsRow<Float>
) {
    constructor (mediaStatistics: MediaStats) : this(
        packetsLost = MediaStatisticsRow(
            input = max(
                mediaStatistics.audioRx.windowedPacketsLostPercentage,
                max(
                    mediaStatistics.videoRx.windowedPacketsLostPercentage,
                    mediaStatistics.slidesRx.windowedPacketsLostPercentage,
                )
            ),
            output = max(
                mediaStatistics.audioTx.windowedPacketsLostPercentage,
                max(
                    mediaStatistics.videoTx.windowedPacketsLostPercentage,
                    mediaStatistics.slidesTx.windowedPacketsLostPercentage,
                ),
            )
        )
    )
}
