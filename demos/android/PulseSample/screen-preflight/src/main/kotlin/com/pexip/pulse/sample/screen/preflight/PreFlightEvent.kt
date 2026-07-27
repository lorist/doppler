package com.pexip.pulse.sample.screen.preflight

import android.net.Uri
import com.pexip.pulse.conference.SsoProvider
import com.slack.circuit.runtime.CircuitUiEvent

sealed interface PreFlightEvent : CircuitUiEvent {
    data object MuteAudio : PreFlightEvent
    data object MuteVideo : PreFlightEvent
    data object FlipCamera : PreFlightEvent
    data object ToggleBlur : PreFlightEvent
    data object DismissErrorAlert : PreFlightEvent
    data object DismissSsoProviderPicker : PreFlightEvent
    data class SelectSsoProvider(val provider: SsoProvider) : PreFlightEvent
    data class HandleSsoData(val data: Uri) : PreFlightEvent
    data object Join : PreFlightEvent
    data object Cancel : PreFlightEvent
    data object Back : PreFlightEvent
    data object Settings : PreFlightEvent
}
