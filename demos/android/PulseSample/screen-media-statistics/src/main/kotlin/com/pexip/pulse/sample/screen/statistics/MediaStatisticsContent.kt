package com.pexip.pulse.sample.screen.statistics

import com.pexip.pulse.media.MediaRxStats
import com.pexip.pulse.media.MediaTxStats

data class MediaStatisticsContent(
    val packetsTransmitted: MediaStatisticsRow<Long>,
    val packetsLost: MediaStatisticsRow<Long>,
    val recentPacketLoss: MediaStatisticsRow<Float>,
    val totalPacketLoss: MediaStatisticsRow<Float>,
    val jitter: MediaStatisticsRow<Float>,
    val bitrate: MediaStatisticsRow<Int>,
    val codec: MediaStatisticsRow<String?>,
    val resolution: MediaStatisticsRow<String?>,
    val roundTripTime: MediaStatisticsRow<Float>,
) {
    constructor(input: MediaRxStats, output: MediaTxStats) : this(
        packetsTransmitted = MediaStatisticsRow(
            input = input.totalPacketsReceived,
            output = output.totalPacketsSent,
        ),
        packetsLost = MediaStatisticsRow(
            input = input.totalPacketsLost,
            output = output.totalPacketsLost,
        ),
        recentPacketLoss = MediaStatisticsRow(
            input = input.windowedPacketsLostPercentage,
            output = output.windowedPacketsLostPercentage,
        ),
        totalPacketLoss = MediaStatisticsRow(
            input = input.totalPacketsLostPercentage,
            output = output.totalPacketsLostPercentage,
        ),
        jitter = MediaStatisticsRow(
            input = input.totalJitterMilliseconds,
            output = output.totalJitterMilliseconds,
        ),
        bitrate = MediaStatisticsRow(
            input = input.totalBitrate / 1000,
            output = output.totalBitrate / 1000,
        ),
        codec = MediaStatisticsRow(
            input = input.encodingName,
            output = output.encodingName,
        ),
        resolution = MediaStatisticsRow(
            input = resolution(
                width = input.width,
                height = input.height,
                frameRate = input.frameRate,
            ),
            output = resolution(
                width = output.width,
                height = output.height,
                frameRate = output.frameRate,
            ),
        ),
        roundTripTime = MediaStatisticsRow(
            input = null,
            output = output.roundTripTimeMilliseconds,
        ),
    )

    companion object {
        fun resolution(width: Int, height: Int, frameRate: Double): String? =
            when (width != 0 && height != 0 && frameRate != 0.0) {
                true -> "${width}x${height}p${frameRate}"
                else -> null
            }
    }
}
