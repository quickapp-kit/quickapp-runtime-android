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
            version = "4.1.2"
        }
    }

    sourceSets["main"].assets.srcDir("${layout.buildDirectory.get().asFile}/generated/case001-assets")

    buildTypes {
        debug {
            isDebuggable = true
        }
        release {
            isMinifyEnabled = false
        }
    }
}

val syncGoldenRpk by tasks.registering(Copy::class) {
    from("../../quickapp-toolkit/evidence/tk-s12-lvgl-p0.rpk") {
        rename { "golden-app.rpk" }
    }
    from("../../quickapp-examples/showcases/gallery-001/dist/gallery-001.rpk")
    from("../../quickapp-examples/showcases/consumer-001/dist/consumer-001.rpk")
    from("../../quickapp-examples/showcases/wearable-001/dist/wearable-001.rpk")
    from("../../quickapp-examples/showcases/commerce-001/dist/commerce-001.rpk")
    from("../../quickapp-examples/showcases/controls-001/dist/controls-001.rpk")
    from("../../quickapp-examples/showcases/controls-002/dist/controls-002.rpk")
    from("../../quickapp-examples/showcases/list-001/dist/list-001.rpk")
    from("../../quickapp-examples/showcases/platform-001/dist/platform-001.rpk")
    from("../../quickapp-examples/showcases/media-001/dist/media-001.rpk")
    from("../../quickapp-examples/showcases/tabs-001/dist/tabs-001.rpk")
    from("../../quickapp-examples/showcases/capability-gallery-001/dist/capability-gallery-001.rpk")
    into(layout.buildDirectory.dir("generated/case001-assets"))
}

tasks.named("preBuild").configure {
    dependsOn(syncGoldenRpk)
}
