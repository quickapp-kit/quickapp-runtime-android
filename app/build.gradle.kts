plugins {
    id("com.android.application")
}

android {
    namespace = "dev.quickapp.kit.host"
    compileSdk = 36
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "dev.quickapp.kit.host"
        minSdk = 28
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"

        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
    }

    sourceSets["main"].assets.srcDir("${layout.buildDirectory.get().asFile}/generated/host-rpk")

    packaging {
        jniLibs.useLegacyPackaging = true
    }

    dependencies {
        implementation(project(":quickapp-runtime-android"))
    }

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
    from(rootProject.file("../quickapp-toolkit/evidence/tk-s07-case001.rpk"))
    from(rootProject.file("../quickapp-toolkit/evidence/tk-s08-binding001.rpk"))
    from(rootProject.file("../quickapp-toolkit/evidence/tk-s09-case002.rpk"))
    from(rootProject.file("../quickapp-toolkit/evidence/tk-s10-block001.rpk"))
    from(rootProject.file("../quickapp-toolkit/evidence/tk-s11-image-input001.rpk"))
    from(rootProject.file("../quickapp-toolkit/evidence/tk-s12-lvgl-p0.rpk"))
    from(rootProject.file("../quickapp-toolkit/evidence/tk-timer-001.rpk"))
    from(rootProject.file("../quickapp-examples/showcases/inspection-board/dist/inspection-board.rpk"))
    from(rootProject.file("../quickapp-examples/showcases/content-hub/dist/content-hub.rpk"))
    from(rootProject.file("../quickapp-examples/showcases/health-summary/dist/health-summary.rpk"))
    from(rootProject.file("../quickapp-examples/baseline-cases/controls-001/dist/controls-001.rpk"))
    from(rootProject.file("../quickapp-examples/baseline-cases/controls-002/dist/controls-002.rpk"))
    from(rootProject.file("../quickapp-examples/baseline-cases/list-001/dist/list-001.rpk"))
    from(rootProject.file("../quickapp-examples/baseline-cases/platform-001/dist/platform-001.rpk"))
    from(rootProject.file("../quickapp-examples/baseline-cases/media-001/dist/media-001.rpk"))
    from(rootProject.file("../quickapp-examples/baseline-cases/tabs-001/dist/tabs-001.rpk"))
    from(rootProject.file("../quickapp-examples/baseline-cases/capability-gallery-001/dist/capability-gallery-001.rpk"))
    from(rootProject.file("../quickapp-examples/baseline-cases/long-list-001/dist/long-list-001.rpk"))
    from(rootProject.file("../quickapp-examples/showcases/shop/dist/shop.rpk"))
    from(rootProject.file("../quickapp-examples/showcases/sport-band/dist/sport-band.rpk"))
    from(rootProject.file("../quickapp-examples/showcases/sport-watch/dist/sport-watch.rpk"))
    from(rootProject.file("../quickapp-examples/baseline-cases/url-001/dist/url-001.rpk"))
    from(rootProject.file("../quickapp-examples/showcases/card-wallet/dist/card-wallet.rpk"))
    into(layout.buildDirectory.dir("generated/host-rpk"))
}

tasks.named("preBuild").configure {
    dependsOn(syncGoldenRpk)
}
