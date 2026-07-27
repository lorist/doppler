package com.pexip.pulse.sample

import com.slack.circuit.foundation.Circuit
import dagger.Module
import dagger.Provides
import dagger.hilt.InstallIn
import dagger.hilt.components.SingletonComponent
import javax.inject.Singleton

@Module
@InstallIn(SingletonComponent::class)
object CircuitModule {

    @Provides
    @Singleton
    fun provideCircuit(
        uiFactory: SampleUiFactory,
        presenterFactory: SamplePresenterFactory,
    ): Circuit = Circuit.Builder()
        .addUiFactory(uiFactory)
        .addPresenterFactory(presenterFactory)
        .build()
}
