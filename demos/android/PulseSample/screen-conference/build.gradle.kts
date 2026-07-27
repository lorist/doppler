plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.kotlin.parcelize)
    alias(libs.plugins.hilt)
    alias(libs.plugins.ksp)
}

android {
    namespace = "com.pexip.pulse.sample.screen.conference"
    compileSdk = Config.COMPILE_SDK
}

dependencies {
    implementation(project(":screen-settings"))
    implementation(project(":screen-chat"))
    implementation(project(":screen-roster"))
    implementation(project(":util-design"))
    implementation(project(":util-media"))
    implementation(libs.pexip.pulse)
    implementation(libs.material)
    implementation(libs.circuit.foundation)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.material.icons.extended)
    implementation(libs.androidx.compose.material3.windowsizeclass)
    implementation(libs.circuit.runtime)
    implementation(libs.dagger.hilt)
    ksp(libs.dagger.hilt.compiler)
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.appcompat)
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
}
