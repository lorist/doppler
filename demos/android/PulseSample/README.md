# Pexip Pulse Android SDK - Sample Application

This is a sample Android application demonstrating how to use the Pexip Pulse SDK for video conferencing.

## Prerequisites

- Android Studio (latest stable version recommended)
- Android SDK with API level 29 or higher
- JDK 17

## SDK Dependency

The sample app resolves the Pexip Pulse SDK (`com.pexip.pulse:pulse-core`) from
`mavenLocal()`. Before building, you must place the SDK artifacts in your local
Maven repository.

### Setting up Maven Local

Maven Local (`~/.m2/repository`) uses a directory structure based on Maven
coordinates. Place the downloaded SDK files (`.aar`, `.pom`, etc.) at:

```
~/.m2/repository/com/pexip/pulse/pulse-core/<VERSION>/
```

Where `<VERSION>` matches the version specified in `gradle/libs.versions.toml`.

For example, if the version is `1.0.0`:

```
~/.m2/repository/com/pexip/pulse/pulse-core/1.0.0/
├── pulse-core-1.0.0.aar
├── pulse-core-1.0.0.pom
└── pulse-core-1.0.0.module
```

## Getting Started

1. Open this project in Android Studio.
2. Sync the project with Gradle files.
3. Build and run the application on a device or emulator with API level 29+.

## License

This sample application is provided as-is for demonstration purposes.
See the Pexip Pulse SDK license for terms of use.
