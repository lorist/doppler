package com.pexip.pulse.sample

import android.app.Application
import com.pexip.pulse.core.DebugLevel
import com.pexip.pulse.core.Pulse
import com.pexip.pulse.core.PulseAdvancedApi
import dagger.hilt.android.HiltAndroidApp

@HiltAndroidApp
class SampleApplication : Application() {
    lateinit var pulse: Pulse

    @OptIn(PulseAdvancedApi::class)
    override fun onCreate() {
        super.onCreate()
        pulse = Pulse.Builder(this)
            // Advanced: fine-grained GStreamer debug control
            .debugLevel(DebugLevel.custom("WARN,*pulse*:DEBUG"))
            // Or simply: .debugLevel(DebugLevel.WARNING)
            .build()
    }
}
