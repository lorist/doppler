package com.pexip.pulse.sample.screen.home

import android.content.Context
import android.content.pm.PackageManager
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import dagger.hilt.android.qualifiers.ApplicationContext
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import javax.inject.Inject

internal class PermissionChecker @Inject constructor(
    @param:ApplicationContext private val context: Context,
    private val owner: LifecycleOwner,
) {
    suspend fun awaitPermission(permission: String) {
        owner.lifecycle.currentStateFlow
            .filter { it.isAtLeast(Lifecycle.State.CREATED) }
            .map { ContextCompat.checkSelfPermission(context, permission) }
            .first { it == PackageManager.PERMISSION_GRANTED }
    }
}
