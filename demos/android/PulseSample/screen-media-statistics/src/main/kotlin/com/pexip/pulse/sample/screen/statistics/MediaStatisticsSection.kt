package com.pexip.pulse.sample.screen.statistics

sealed interface MediaStatisticsSection {

    data class Audio(val content: MediaStatisticsContent) : MediaStatisticsSection

    data class Video(val content: MediaStatisticsContent) : MediaStatisticsSection

    data class Presentation(val content: MediaStatisticsContent) : MediaStatisticsSection

    data class Quality(val content: MediaStatisticsQualityContent) : MediaStatisticsSection
}
