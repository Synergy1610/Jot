import java.util.Properties
import java.net.URI

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

val localProperties = Properties().apply {
    val file = rootProject.file("local.properties")
    if (file.exists()) file.inputStream().use { load(it) }
}

val secretsProperties = Properties().apply {
    val file = rootProject.file("secrets.properties")
    if (file.exists()) file.inputStream().use { load(it) }
}

fun setting(name: String): String =
    System.getenv(name)
        ?.takeIf { it.isNotBlank() }
        ?: secretsProperties.getProperty(name)?.takeIf { it.isNotBlank() }
        ?: localProperties.getProperty(name)?.takeIf { it.isNotBlank() }
        ?: ""

fun requiredSupabaseUrl(): String {
    val value = setting("SUPABASE_URL").trim()
    val parsed = runCatching { URI(value) }.getOrNull()
    require(parsed != null && parsed.scheme == "https" && !parsed.host.isNullOrBlank()) {
        "SUPABASE_URL must be an absolute https:// URL. Put it in secrets.properties."
    }
    return value
}

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

        buildConfigField("String", "SUPABASE_URL", quoted(requiredSupabaseUrl()))
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
        resources {
            excludes += "/META-INF/{AL2.0,LGPL2.1}"
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
    implementation("com.squareup.okhttp3:okhttp:4.12.0")

    debugImplementation("androidx.compose.ui:ui-tooling")

    testImplementation("junit:junit:4.13.2")
}
