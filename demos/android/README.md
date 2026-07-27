# android — Pexip Pulse on Android

A sample Android application showcasing the **Pexip Pulse Android SDK**
(`com.pexip.pulse:pulse-core`): preflight checks, joining a conference, the
roster, in-call media, chat, screen sharing and media statistics.

Unlike the C/C++ demos in this repo, this one is a standalone **Gradle**
project rather than a CMake target, and it ships its own copy of the Pulse
Android SDK as a local Maven repository:

| Path | Purpose |
| ---- | ------- |
| `PulseSample/` | The Android Studio project (open this folder). |
| `local-repo/` | The Pulse Android SDK published as a `mavenLocal()` repo. |
| `1.0.x+<sha>/` | The raw SDK artifacts (`.aar`, `.pom`, demo zip, license). |

## Build & run

Open `PulseSample/` in Android Studio (or build from the command line with
the Gradle wrapper). The full prerequisites, SDK-resolution notes and feature
walk-through live in [`PulseSample/README.md`](PulseSample/README.md).
