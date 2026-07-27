pluginManagement {
    repositories {
        google {
            content {
                includeGroupByRegex("com\\.android.*")
                includeGroupByRegex("com\\.google.*")
                includeGroupByRegex("androidx.*")
            }
        }
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    @Suppress("UnstableApiUsage")
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)

    @Suppress("UnstableApiUsage")
    repositories {
        maven {
            url = uri(rootDir.resolve("../local-repo"))
        }
        mavenLocal()
        google()
        mavenCentral()
    }
}

rootProject.name = "pexip-pulse-android-sample"

include(":app")
include(":screen-home")
include(":screen-preflight")
include(":screen-conference")
include(":util-design")
include(":util-media")
include(":screen-settings")
include(":screen-media-statistics")
include(":screen-roster")
include(":screen-chat")
