plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.kotlin.parcelize)
}

android {
    namespace = "com.pexip.pulse.sample.screen.home"
    compileSdk = Config.COMPILE_SDK
}

dependencies {
    implementation(project(":screen-preflight"))
    implementation(project(":screen-conference"))
    implementation(project(":util-media"))
    implementation(libs.pexip.pulse)
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.ui.tooling.preview.android)
    implementation(libs.dagger.hilt)
    implementation(libs.datastore.preferences)
    implementation(libs.material)
    implementation(libs.circuit.foundation)
    implementation(libs.circuit.runtime)
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
}
