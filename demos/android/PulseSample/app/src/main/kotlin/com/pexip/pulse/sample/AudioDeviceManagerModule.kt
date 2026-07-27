package com.pexip.pulse.sample

import android.content.Context
import com.pexip.sdk.media.AudioDeviceManager
import com.pexip.sdk.media.android.create
import dagger.Module
import dagger.Provides
import dagger.hilt.InstallIn
import dagger.hilt.android.qualifiers.ApplicationContext
import dagger.hilt.components.SingletonComponent
import javax.inject.Singleton

@Module
@InstallIn(SingletonComponent::class)
object AudioDeviceManagerModule {
    @Provides
    @Singleton
    fun provideAudioDeviceManager(@ApplicationContext context: Context): AudioDeviceManager =
        AudioDeviceManager.create(context)
}
