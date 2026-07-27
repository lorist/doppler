plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "com.pexip.pulse.sample.util.media"
    compileSdk = Config.COMPILE_SDK
}

dependencies {
    implementation(libs.pexip.pulse)
    implementation(libs.dagger.hilt)
    implementation(libs.androidx.core.ktx)
    testImplementation(libs.junit)
}

