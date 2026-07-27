plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.kotlin.parcelize)
}

android {
    namespace = "com.pexip.pulse.sample.screen.preflight"
    compileSdk = Config.COMPILE_SDK
}

dependencies {
    implementation(project(":screen-conference"))
    implementation(project(":screen-settings"))
    implementation(project(":util-design"))
    implementation(project(":util-media"))
    implementation(libs.pexip.pulse)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.material.icons.extended)
    implementation(libs.circuit.foundation)
    implementation(libs.circuit.runtime)
    implementation(libs.dagger.hilt)
    implementation(libs.material)
    implementation(libs.androidx.browser)
    implementation(libs.androidx.foundation)
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
}
