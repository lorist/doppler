plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.hilt)
    alias(libs.plugins.ksp)
}

android {
    namespace = "com.pexip.pulse.sample"
    compileSdk = Config.COMPILE_SDK
    defaultConfig {
        applicationId = "com.pexip.pulse.sample"
        minSdk = Config.MIN_SDK
        targetSdk = Config.TARGET_SDK
        versionCode = 1
        versionName = "1.0"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }
    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }
    packaging {
        resources {
            excludes += "META-INF/gradle/incremental.annotation.processors"
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.toVersion(Config.JAVA_VERSION)
        targetCompatibility = JavaVersion.toVersion(Config.JAVA_VERSION)
    }

    buildFeatures {
        compose = true
    }
}

dependencies {
    implementation(project(":screen-home"))
    implementation(project(":screen-preflight"))
    implementation(project(":screen-conference"))
    implementation(project(":screen-settings"))
    implementation(project(":screen-media-statistics"))
    implementation(project(":screen-roster"))
    implementation(project(":screen-chat"))
    implementation(project(":util-media"))
    implementation(libs.pexip.pulse)
    implementation(libs.pexip.sdk.media)
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.activity.compose)
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.ui)
    implementation(libs.androidx.ui.graphics)
    implementation(libs.androidx.ui.tooling.preview)
    implementation(libs.circuit.foundation)
    implementation(libs.circuit.runtime)
    implementation(libs.circuit.overlay)
    implementation(libs.dagger.hilt)
    ksp(libs.dagger.hilt.compiler)
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.compose.material3)
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
    androidTestImplementation(platform(libs.androidx.compose.bom))
    androidTestImplementation(libs.androidx.ui.test.junit4)
    debugImplementation(libs.androidx.ui.tooling)
    debugImplementation(libs.androidx.ui.test.manifest)
}
