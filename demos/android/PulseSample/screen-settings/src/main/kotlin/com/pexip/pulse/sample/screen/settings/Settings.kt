package com.pexip.pulse.sample.screen.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import com.pexip.pulse.sample.util.design.BackButton

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun Settings(
    state: SettingsState,
    modifier: Modifier = Modifier,
) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Settings") },
                navigationIcon = {
                    BackButton(
                        onClick = { state.onEvent(SettingsEvent.Back) },
                    )
                }
            )
        },
        modifier = modifier,
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .consumeWindowInsets(it)
                .padding(it)
        ) {
            Column(
                verticalArrangement = Arrangement.spacedBy(16.dp),
                modifier = Modifier
                    .width(8.dp * 70)
                    .padding(vertical = 16.dp)
                    .verticalScroll(rememberScrollState())
            ) {
                DevicePicker(
                    label = "Audio",
                    devices = state.audioDevices,
                    currentDevice = state.currentAudioDevice,
                    name = { "${it.name} (${it.type})" },
                    onDeviceSelected = {
                        state.onEvent(SettingsEvent.AudioDeviceSelected(it))
                    },
                )
                if (!state.rtmpEnabled) {
                    DevicePicker(
                        label = "Video",
                        devices = (listOf(null) + state.videoDevices),
                        currentDevice = state.currentVideoDevice,
                        name = { it.name },
                        onDeviceSelected = {
                            state.onEvent(SettingsEvent.VideoDeviceSelected(it))
                        },
                    )
                }
                Button(
                    onClick = { state.onEvent(SettingsEvent.MediaStatistics) },
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text("Media statistics")
                }
            }
        }
    }
}

@Composable
private fun <T> DevicePicker(
    label: String,
    devices: List<T?>,
    currentDevice: T?,
    name: (T) -> String,
    onDeviceSelected: (T?) -> Unit,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = Modifier.padding(horizontal = 8.dp)
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelLarge,
            color = MaterialTheme.colorScheme.primary,
            modifier = Modifier.padding(horizontal = 16.dp),
        )
        Column(modifier.selectableGroup()) {
            devices.forEach { device ->
                Row(
                    Modifier
                        .fillMaxWidth()
                        .height(56.dp)
                        .selectable(
                            selected = (device == currentDevice),
                            onClick = { onDeviceSelected(device) },
                            role = Role.RadioButton
                        )
                        .padding(horizontal = 16.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    RadioButton(
                        selected = (device == currentDevice),
                        onClick = null
                    )
                    Text(
                        text = device?.let { name(it) } ?: "None",
                        style = MaterialTheme.typography.bodyLarge,
                        modifier = Modifier.padding(start = 16.dp)
                    )
                }
            }
        }
    }
}
