package com.pexip.pulse.sample.screen.home

import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp

@Composable
fun Home(
    state: HomeState,
    modifier: Modifier = Modifier,
) {
    val focusManager = LocalFocusManager.current
    val scrollState = rememberScrollState()
    Surface {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center,
            modifier = modifier
                .fillMaxSize()
                .windowInsetsPadding(WindowInsets.safeDrawing)
                .verticalScroll(scrollState)
                .pointerInput(Unit) {
                    detectTapGestures(onTap = { focusManager.clearFocus() })
                }
                .imePadding()
        ) {
            Column(
                verticalArrangement = Arrangement.spacedBy(16.dp),
                modifier = Modifier
                    .width(8.dp * 70)
                    .padding(start = 16.dp, end = 16.dp)
            ) {
                Text(
                    "Join the conference",
                    style = MaterialTheme.typography.headlineMedium
                )
                TextField(
                    value = state.displayName,
                    onValueChange = { state.onEvent(HomeEvent.DisplayNameChanged(it)) },
                    label = { Text("Display name") },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Text),
                    textStyle = MaterialTheme.typography.bodyLarge,
                    modifier = Modifier.fillMaxWidth()
                )
                TextField(
                    value = state.conferenceAlias,
                    onValueChange = { state.onEvent(HomeEvent.ConferenceAliasChanged(it)) },
                    label = { Text("Conference alias") },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Text),
                    modifier = Modifier.fillMaxWidth()
                )
                TextField(
                    value = state.serverAddress,
                    onValueChange = { state.onEvent(HomeEvent.ServerAddressChanged(it)) },
                    label = { Text("Server address") },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri),
                    modifier = Modifier.fillMaxWidth()
                )
                TextField(
                    value = state.pin,
                    onValueChange = { state.onEvent(HomeEvent.PinChanged(it)) },
                    label = { Text("Host or guest PIN (optional)") },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(
                        keyboardType = KeyboardType.NumberPassword
                    ),
                    modifier = Modifier.fillMaxWidth()
                )
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(8.dp)
                        .clickable { state.onEvent(HomeEvent.ToggleRtmp) }
                ) {
                    Checkbox(
                        checked = state.rtmpEnabled,
                        onCheckedChange = null
                    )
                    Text("Start RTMP server")
                }
                Button(
                    onClick = { state.onEvent(HomeEvent.JoinConference) },
                    modifier = Modifier.fillMaxWidth().padding(top = 16.dp)
                ) {
                    Text("Next")
                }
            }
        }
    }
}

@Preview
@Composable
fun HomePreview() {
    Home(
        state = HomeState(
            displayName = "John Doe",
            conferenceAlias = "example-conference",
            serverAddress = "pexip.com",
            pin = "",
            rtmpEnabled = false,
            onEvent = {}
        ),
        modifier = Modifier.padding(16.dp),
    )
}
