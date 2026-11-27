import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

val localProperties = Properties().apply {
    val file = rootProject.file("local.properties")
    if (file.exists()) file.inputStream().use { load(it) }
}

fun setting(name: String): String =
    System.getenv(name) ?: localProperties.getProperty(name) ?: ""

fun quoted(value: String): String =
    "\"" + value.replace("\\", "\\\\").replace("\"", "\\\"") + "\""

android {
    namespace = "app.jot"
    compileSdk = 35

    defaultConfig {
        applicationId = "app.jot"
        minSdk = 26
        targetSdk = 35
        versionCode = setting("JOT_VERSION_CODE").ifEmpty { "1" }.toInt()
        versionName = setting("JOT_VERSION").ifEmpty { "0.1.0" }

        buildConfigField("String", "SUPABASE_URL", quoted(setting("SUPABASE_URL")))
        buildConfigField("String", "SUPABASE_ANON_KEY", quoted(setting("SUPABASE_ANON_KEY")))
    }

    buildFeatures {
        compose = true
        buildConfig = true
    }

    // Release builds are signed with the project keystore when one is
    // configured; otherwise the debug key is used so a sideloadable APK is
    // always produced.
    signingConfigs {
        val store = setting("JOT_KEYSTORE")
        if (store.isNotEmpty() && file(store).exists()) {
            create("release") {
                storeFile = file(store)
                storePassword = setting("JOT_KEYSTORE_PASSWORD")
                keyAlias = setting("JOT_KEY_ALIAS")
                keyPassword = setting("JOT_KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        debug {
            applicationIdSuffix = ".debug"
            versionNameSuffix = "-debug"
        }
        release {
            isMinifyEnabled = false
            isShrinkResources = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfig = signingConfigs.findByName("release")
                ?: signingConfigs.getByName("debug")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    packaging {
        jniLibs {
            useLegacyPackaging = false
        }
    }
}

dependencies {
    val compose = platform("androidx.compose:compose-bom:2024.12.01")
    implementation(compose)
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.foundation:foundation")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.core:core-ktx:1.15.0")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.7")
    implementation("androidx.work:work-runtime-ktx:2.10.0")

    debugImplementation("androidx.compose.ui:ui-tooling")

    testImplementation("junit:junit:4.13.2")
}
