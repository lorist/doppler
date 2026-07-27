plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.kotlin.parcelize)
}

android {
    namespace = "com.pexip.pulse.sample.screen.chat"
    compileSdk = Config.COMPILE_SDK
}

dependencies {
    implementation(project(":util-design"))
    implementation(libs.pexip.pulse)
    implementation(libs.kotlinx.collections.immutable)
    implementation(libs.material)
    implementation(libs.circuit.foundation)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.material.icons.extended)
    implementation(libs.androidx.ui.tooling)
    implementation(libs.circuit.runtime)
    implementation(libs.dagger.hilt)
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.appcompat)
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
}
