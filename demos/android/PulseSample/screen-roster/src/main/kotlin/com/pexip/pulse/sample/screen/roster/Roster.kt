package com.pexip.pulse.sample.screen.roster

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.ScreenShare
import androidx.compose.material.icons.filled.CenterFocusStrong
import androidx.compose.material.icons.filled.MicOff
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.VideocamOff
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import com.pexip.pulse.conference.ConferenceCallDirection
import com.pexip.pulse.conference.ConferenceRole
import com.pexip.pulse.conference.Participant
import kotlinx.collections.immutable.ImmutableList
import kotlinx.collections.immutable.persistentListOf
import kotlin.uuid.ExperimentalUuidApi
import kotlin.uuid.Uuid

@Composable
fun Roster(
    state: RosterState,
    modifier: Modifier = Modifier,
) {
    Roster(
        me = state.me,
        participants = state.participants,
        locked = state.locked,
        allGuestsMuted = state.allGuestsMuted,
        guestsCanUnmute = state.guestsCanUnmute,
        onEvent = state.onEvent,
        modifier = modifier,
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun Roster(
    modifier: Modifier = Modifier,
    me: Participant? = null,
    participants: ImmutableList<Participant>,
    locked: Boolean = false,
    allGuestsMuted: Boolean = false,
    guestsCanUnmute: Boolean = true,
    onEvent: (RosterEvent) -> Unit,
) {
    val canManage = me?.role == ConferenceRole.HOST
    Scaffold(
        modifier = modifier,
        topBar = {
            TopAppBar(
                title = { Text("Participants") },
                navigationIcon = {
                    IconButton(onClick = { onEvent(RosterEvent.Back) }) {
                        Icon(
                            imageVector = Icons.AutoMirrored.Filled.ArrowBack,
                            contentDescription = "Back",
                        )
                    }
                },
                actions = {
                    var menuExpanded by remember(canManage) { mutableStateOf(false) }
                    if (canManage) {
                        IconButton(onClick = { menuExpanded = true }) {
                            Icon(
                                imageVector = Icons.Filled.MoreVert,
                                contentDescription = "Conference menu",
                            )
                        }
                        ConferenceMenu(
                            expanded = menuExpanded,
                            locked = locked,
                            allGuestsMuted = allGuestsMuted,
                            guestsCanUnmute = guestsCanUnmute,
                            onDismissRequest = { menuExpanded = false },
                            onClick = {
                                onEvent(it)
                                menuExpanded = false
                            }
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
            LazyColumn(
                contentPadding = PaddingValues(8.dp),
            ) {
                items(participants.size) { index ->
                    ParticipantRow(
                        participant = participants[index],
                        canManage = canManage,
                        onClick = onEvent
                    )
                }
            }
        }
    }
}

@Composable
private fun ParticipantRow(
    participant: Participant,
    canManage: Boolean,
    onClick: (RosterEvent) -> Unit
) {
    var menuExpanded by remember(canManage) { mutableStateOf(false) }

    Box(modifier = Modifier.fillMaxWidth()) {
        ParticipantContent(
            participant = participant,
            modifier = Modifier.clickable { menuExpanded = true }
        )
        if (canManage) {
            ParticipantMenu(
                participant = participant,
                expanded = menuExpanded,
                onDismissRequest = { menuExpanded = false },
                onClick = {
                    onClick(it)
                    menuExpanded = false
                }
            )
        }
    }
}

@Composable
private fun ParticipantContent(
    participant: Participant,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier = modifier
            .padding(16.dp)
            .fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = buildString {
                append(participant.name)
                if (participant.isLocalParticipant) {
                    append(" / You")
                }
                if (participant.role == ConferenceRole.HOST) {
                    append(" / Host")
                }
            },
            style = MaterialTheme.typography.bodyLarge,
            modifier = Modifier.weight(1f)
        )
        if (participant.isMuted) {
            Icon(
                imageVector = Icons.Filled.MicOff,
                contentDescription = "Audio Muted",
                tint = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
        if (participant.isVideoMuted) {
            Icon(
                imageVector = Icons.Filled.VideocamOff,
                contentDescription = "Video Muted",
                tint = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
        if (participant.hasSpotlight) {
            Icon(
                imageVector = Icons.Filled.CenterFocusStrong,
                contentDescription = "Spotlighted",
                tint = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
        if (participant.isPresenting) {
            Icon(
                imageVector = Icons.AutoMirrored.Filled.ScreenShare,
                contentDescription = "Presenting",
                tint = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
private fun ParticipantMenu(
    participant: Participant,
    expanded: Boolean,
    onDismissRequest: () -> Unit,
    onClick: (RosterEvent) -> Unit
) {
    DropdownMenu(
        expanded = expanded,
        onDismissRequest = onDismissRequest
    ) {
        if (participant.raisedHand) {
            DropdownMenuItem(
                text = { Text("Lower hand") },
                onClick = { onClick(RosterEvent.LowerHand(participant)) }
            )
        }
        if (participant.hasSpotlight) {
            DropdownMenuItem(
                text = { Text("Remove spotlight") },
                onClick = { onClick(RosterEvent.RemoveSpotlight(participant)) }
            )
        } else {
            DropdownMenuItem(
                text = { Text("Spotlight") },
                onClick = { onClick(RosterEvent.AddSpotlight(participant)) }
            )
        }
        if (!participant.isLocalParticipant) {
            if (participant.isServerMuted) {
                DropdownMenuItem(
                    text = { Text("Unmute microphone") },
                    onClick = { onClick(RosterEvent.UnmuteAudio(participant)) }
                )
            } else {
                DropdownMenuItem(
                    text = { Text("Mute microphone") },
                    onClick = { onClick(RosterEvent.MuteAudio(participant)) }
                )
            }
            if (participant.role == ConferenceRole.HOST) {
                DropdownMenuItem(
                    text = { Text("Make guest") },
                    onClick = { onClick(RosterEvent.MakeGuest(participant)) }
                )
            } else {
                DropdownMenuItem(
                    text = { Text("Make host") },
                    onClick = { onClick(RosterEvent.MakeHost(participant)) }
                )
            }
            DropdownMenuItem(
                text = { Text("Disconnect", color = MaterialTheme.colorScheme.error) },
                onClick = { onClick(RosterEvent.Disconnect(participant)) }
            )
        }
    }
}

@Composable
private fun ConferenceMenu(
    expanded: Boolean,
    locked: Boolean,
    allGuestsMuted: Boolean,
    guestsCanUnmute: Boolean,
    onDismissRequest: () -> Unit,
    onClick: (RosterEvent) -> Unit
) {
    DropdownMenu(
        expanded = expanded,
        onDismissRequest = onDismissRequest
    ) {
        if (locked) {
            DropdownMenuItem(
                text = { Text("Unlock") },
                onClick = { onClick(RosterEvent.Unlock) }
            )
        } else {
            DropdownMenuItem(
                text = { Text("Lock") },
                onClick = { onClick(RosterEvent.Lock) }
            )
        }

        if (allGuestsMuted) {
            DropdownMenuItem(
                text = { Text("Unmute all guests") },
                onClick = { onClick(RosterEvent.UnmuteAllGuests) }
            )
        } else {
            DropdownMenuItem(
                text = { Text("Mute all guests") },
                onClick = { onClick(RosterEvent.MuteAllGuests) }
            )
        }

        if (guestsCanUnmute) {
            DropdownMenuItem(
                text = { Text("Disallow guests to unmute") },
                onClick = { onClick(RosterEvent.DisallowGuestsToUnmute) }
            )
        } else {
            DropdownMenuItem(
                text = { Text("Allow guests to unmute") },
                onClick = { onClick(RosterEvent.AllowGuestsToUnmute) }
            )
        }
    }
}

@OptIn(ExperimentalUuidApi::class)
@Preview
@Composable
fun RosterPreview() {
    Roster(
        participants = persistentListOf(
            Participant(
                uuid = Uuid.random().toString(),
                displayName = "Alice Johnson",
                overlayText = "Alice Johnson",
                role = ConferenceRole.HOST,
                isLocalParticipant = true,
                callDirection = ConferenceCallDirection.INBOUND
            ),
            Participant(
                uuid = Uuid.random().toString(),
                displayName = "Bob Smith",
                overlayText = "Bob Smith",
                role = ConferenceRole.GUEST,
                isServerMuted = true,
                isVideoMuted = true,
                callDirection = ConferenceCallDirection.INBOUND
            ),
            Participant(
                uuid = Uuid.random().toString(),
                displayName = "Charlie Davis",
                overlayText = "Charlie Davis",
                role = ConferenceRole.GUEST,
                isPresenting = true,
                callDirection = ConferenceCallDirection.INBOUND
            )
        ),
        onEvent = {}
    )
}
