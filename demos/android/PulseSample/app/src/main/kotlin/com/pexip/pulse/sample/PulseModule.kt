package com.pexip.pulse.sample

import android.content.Context
import com.pexip.pulse.core.Pulse
import dagger.Module
import dagger.Provides
import dagger.hilt.InstallIn
import dagger.hilt.android.qualifiers.ApplicationContext
import dagger.hilt.components.SingletonComponent
import javax.inject.Singleton

@Module
@InstallIn(SingletonComponent::class)
object PulseModule {
    @Provides
    @Singleton
    fun providePulse(@ApplicationContext context: Context): Pulse {
        return (context as SampleApplication).pulse
    }
}
