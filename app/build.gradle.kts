plugins {
    id("com.android.application")
}

android {
    namespace = "dev.quickapp.kit.android"
    compileSdk = 36
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "dev.quickapp.kit.android"
        minSdk = 28
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"

        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++20")
                arguments += listOf("-DANDROID_STL=c++_shared")
            }
        }
        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../CMakeLists.txt")
            version = "3.22.1"
        }
    }

    sourceSets["main"].assets.srcDir(layout.buildDirectory.dir("generated/case001-assets"))

    buildTypes {
        debug {
            isDebuggable = true
        }
        release {
            isMinifyEnabled = false
        }
    }
}

val syncCase001Rpk by tasks.registering(Copy::class) {
    from("../../quickapp-toolkit/evidence/tk-s07-case001.rpk")
    into(layout.buildDirectory.dir("generated/case001-assets"))
    rename { "case001.rpk" }
}

tasks.named("preBuild").configure {
    dependsOn(syncCase001Rpk)
}
