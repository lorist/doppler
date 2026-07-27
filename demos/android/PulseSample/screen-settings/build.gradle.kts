plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.kotlin.parcelize)
}

android {
    namespace = "com.pexip.pulse.sample.screen.settings"
    compileSdk = Config.COMPILE_SDK
}

dependencies {
    implementation(project(":screen-media-statistics"))
    implementation(project(":util-design"))
    implementation(project(":util-media"))
    implementation(libs.pexip.pulse)
    implementation(libs.pexip.sdk.media)
    implementation(libs.material)
    implementation(libs.circuit.foundation)
    implementation(libs.androidx.compose.material3)
    implementation(libs.circuit.runtime)
    implementation(libs.dagger.hilt)
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.appcompat)
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
}
