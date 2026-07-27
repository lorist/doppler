package com.pexip.pulse.sample.screen.chat

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.rememberCoroutineScope
import com.pexip.pulse.conference.Message
import com.pexip.pulse.conference.Messenger
import com.pexip.pulse.core.Pulse
import com.slack.circuit.retained.rememberRetained
import com.slack.circuit.runtime.Navigator
import com.slack.circuit.runtime.presenter.Presenter
import kotlinx.collections.immutable.toPersistentList
import kotlinx.coroutines.launch
import javax.inject.Inject
import javax.inject.Singleton

class ChatPresenter(
    private val navigator: Navigator,
    private val messenger: Messenger
) : Presenter<ChatState> {

    @Composable
    override fun present(): ChatState {
        val scope = rememberCoroutineScope()
        val messages = rememberRetained { mutableStateListOf<Message>() }

        LaunchedEffect(Unit) {
            messenger.message.collect { message ->
                messages.add(message)
            }
        }

        return ChatState(
            messages = messages.toPersistentList(),
            onEvent = { event ->
                when (event) {
                    is ChatEvent.Back -> navigator.pop()

                    is ChatEvent.SendMessage -> scope.launch {
                        val message = messenger.send(payload = event.text)
                        messages.add(message)
                    }
                }
            }
        )
    }

    @Singleton
    class Factory @Inject constructor(private val pulse: Pulse) {
        fun create(navigator: Navigator): Presenter<ChatState> =
            ChatPresenter(
                navigator = navigator,
                messenger = pulse.conference.messenger,
            )
    }
}
