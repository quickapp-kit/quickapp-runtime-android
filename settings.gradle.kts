pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "QuickAppAndroidRuntime"
include(":quickapp-runtime-android", ":quickapp-host")
project(":quickapp-runtime-android").projectDir = file("runtime")
project(":quickapp-host").projectDir = file("app")
