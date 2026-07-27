package com.pexip.pulse.sample.screen.statistics

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyGridScope
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import kotlinx.collections.immutable.ImmutableList
import kotlinx.collections.immutable.persistentListOf
import java.util.Locale

@Composable
fun MediaStatistics(
    state: MediaStatisticsState,
    modifier: Modifier = Modifier,
) {
    MediaStatistics(
        onBackClick = { state.onEvent(MediaStatisticsEvent.Back) },
        mediaStatisticTypes = state.sections,
        modifier = modifier,
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun MediaStatistics(
    onBackClick: () -> Unit,
    mediaStatisticTypes: ImmutableList<MediaStatisticsSection>,
    modifier: Modifier = Modifier,
) {
    Scaffold(
        modifier = modifier,
        topBar = {
            TopAppBar(
                title = { Text("Media statistics") },
                navigationIcon = {
                    IconButton(onClick = onBackClick) {
                        Icon(
                            imageVector = Icons.AutoMirrored.Filled.ArrowBack,
                            contentDescription = "Back",
                        )
                    }
                }
            )
        },
    ) {
        Surface(modifier = Modifier.fillMaxWidth()
            .consumeWindowInsets(it)
            .padding(it)
        ) {
            LazyVerticalGrid(
                columns = GridCells.Fixed(3),
                contentPadding = PaddingValues(horizontal = 8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                mediaStatisticTypes.forEach { statistics ->
                    section(statistics)
                    emptyRow()
                }
            }
        }
    }
}

private fun LazyGridScope.section(statistics: MediaStatisticsSection) {
    when (statistics) {
        is MediaStatisticsSection.Audio -> {
            sectionHeader("Audio")
            sectionRows(key = "audio", section = statistics.content)
        }
        is MediaStatisticsSection.Video -> {
            sectionHeader("Video")
            sectionRows(key = "video", section = statistics.content)
        }
        is MediaStatisticsSection.Presentation -> {
            sectionHeader("Presentation")
            sectionRows(key = "presentation", section = statistics.content)
        }
        is MediaStatisticsSection.Quality -> {
            qualityHeader(section = statistics.content)
        }
    }
}

private fun LazyGridScope.sectionHeader(category: String) = stickyHeader {
    Row(
        modifier = Modifier.fillMaxWidth()
            .background(Color.White),
    ) {
        Text(
            text = category,
            style = MaterialTheme.typography.titleMedium,
            color = Color.Black,
            modifier = Modifier.weight(1f)
        )
        Text(
            text = "In",
            style = MaterialTheme.typography.titleMedium,
            color = Color.Black,
            modifier = Modifier.weight(1f)
        )
        Text(
            text = "Out",
            style = MaterialTheme.typography.titleMedium,
            color = Color.Black,
            modifier = Modifier.weight(1f),
        )
    }
}

private fun LazyGridScope.qualityHeader(section: MediaStatisticsQualityContent) = stickyHeader {
    Row(Modifier.fillMaxWidth()) {
        val packetsLostIn = section.packetsLost.input ?: 0f
        val packetsLostOut = section.packetsLost.output ?: 0f

        Column(Modifier.weight(1f)) {
            Text(
                text = "Overall",
                style = MaterialTheme.typography.titleMedium,
            )
        }
        Column(Modifier.weight(1f)) {
            Text(
                text = String.format(Locale.US, "%.0f%%", 100 - packetsLostIn),
                style = MaterialTheme.typography.titleMedium
            )
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(getQualityText(packetsLostIn))
                Icon(
                    imageVector = Icons.Filled.KeyboardArrowDown,
                    contentDescription = "Receive",
                )
            }
        }
        Column(Modifier.weight(1f)) {
            Text(
                text = String.format(Locale.US, "%.0f%%", 100 - packetsLostOut),
                style = MaterialTheme.typography.titleMedium
            )

            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(getQualityText(packetsLostOut))
                Icon(
                    imageVector = Icons.Filled.KeyboardArrowUp,
                    contentDescription = "Transmit",
                )
            }
        }
    }
}

private fun LazyGridScope.sectionRows(key: String, section: MediaStatisticsContent) {
    row(
        key = key,
        label = "Packets transmitted",
        row = section.packetsTransmitted,
    )
    row(
        key = key,
        label = "Packets lost",
        row = section.packetsLost,
    )
    row(
        key = key,
        label = "Recent packet loss",
        row = section.recentPacketLoss,
        format = "%.1f%%",
    )
    row(
        key = key,
        label = "Total packet loss",
        row = section.totalPacketLoss,
        format = "%.1f%%",
    )
    row(
        key = key,
        label = "Jitter",
        row = section.jitter,
        format = "%.0f ms",
    )
    row(
        key = key,
        label = "Bitrate",
        row = section.bitrate,
        format = "%d Kbps",
    )
    row(
        key = key,
        label = "Codec",
        row = section.codec,
    )
    row(
        key = key,
        label = "Roundtrip time",
        row = section.roundTripTime,
        format = "%.0f ms",
    )
}

private fun LazyGridScope.row(
    key: String,
    label: String,
    row: MediaStatisticsRow<*>,
    format: String = "%s",
) {
    val itemKey = key.hashCode() + label.hashCode()
    item(key = itemKey) {
        Text(label)
    }
    item(key = "$itemKey+in") {
        val text = when (row.input == null) {
            true -> ""
            else -> String.format(Locale.US, format, row.input)
        }
        Text(text)
    }
    item(key = "$itemKey+out") {
        val text = when (row.output == null) {
            true -> ""
            else -> String.format(Locale.US, format, row.output)
        }
        Text(text)
    }
}

private fun LazyGridScope.emptyRow() = item(
    span = { GridItemSpan(maxLineSpan) }
) {
    Spacer(Modifier.height(24.dp))
}

private fun getQualityText(quality: Float): String = when (quality) {
    in 0f..1f -> "Good"
    in 1f..3f -> "Ok"
    in 3f..10f -> "Bad"
    else -> "Terrible"
}

@Preview
@Composable
fun MediaStatisticsPreview() {
    val qualitySectionContent = MediaStatisticsQualityContent(
        packetsLost = MediaStatisticsRow(
            input = 13f,
            output = 1f,
        )
    )
    val sectionContent = MediaStatisticsContent(
        packetsTransmitted = MediaStatisticsRow(input = 100, output = 100),
        packetsLost = MediaStatisticsRow(input = 10L, output = 0L),
        recentPacketLoss = MediaStatisticsRow(input = 5f, output = 0f),
        totalPacketLoss = MediaStatisticsRow(input = 5f, output = 0f),
        jitter = MediaStatisticsRow(input = 10f, output = 5f),
        bitrate = MediaStatisticsRow(input = 1024, output = 1024),
        codec = MediaStatisticsRow(input = "codec", output = "codec"),
        resolution = MediaStatisticsRow(input = "1920", output = "1080"),
        roundTripTime = MediaStatisticsRow(input = 12f, output = 24f),
    )
    MediaStatistics(
        onBackClick = {},
        mediaStatisticTypes = persistentListOf(
            MediaStatisticsSection.Quality(qualitySectionContent),
            MediaStatisticsSection.Audio(sectionContent),
            MediaStatisticsSection.Video(sectionContent),
            MediaStatisticsSection.Presentation(sectionContent)
        )
    )
}
