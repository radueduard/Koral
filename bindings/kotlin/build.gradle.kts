plugins {
    kotlin("jvm") version "2.4.20" apply false
    kotlin("plugin.compose") version "2.4.20" apply false
}

// Every module: Kotlin on JDK 25 (java.lang.foreign is final from 22), and the native access FFM asks
// to be granted for whatever runs. On macOS, a program also starts on the process's first thread: Cocoa,
// and so a window, works nowhere else, and a JVM's main() is not on it otherwise. (Not tests: they have no
// display.)
val macOS = System.getProperty("os.name").startsWith("Mac")
subprojects {
    plugins.withId("org.jetbrains.kotlin.jvm") {
        extensions.configure<org.jetbrains.kotlin.gradle.dsl.KotlinJvmProjectExtension> {
            compilerOptions { jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_25) }
        }
        extensions.configure<JavaPluginExtension> {
            sourceCompatibility = JavaVersion.VERSION_25
            targetCompatibility = JavaVersion.VERSION_25
        }
    }
    tasks.withType<JavaExec>().configureEach {
        jvmArgs("--enable-native-access=ALL-UNNAMED")
        if (macOS) jvmArgs("-XstartOnFirstThread")
    }
    tasks.withType<Test>().configureEach { jvmArgs("--enable-native-access=ALL-UNNAMED") }
}

// The libraries, as Maven artifacts the SDK carries (share/Koral/maven): `-PkoralRepo=<dir>` publishes them
// there with `publishAllPublicationsToSdkRepository`, at `-PkoralVersion` (Koral's own version).
val koralVersion = (findProperty("koralVersion") as String?) ?: "0.1.0"
val koralRepo = findProperty("koralRepo") as String?
listOf(":koral", ":koral-ui").forEach { path ->
    project(path) {
        apply(plugin = "maven-publish")
        group = "koral"
        version = koralVersion
        plugins.withId("org.jetbrains.kotlin.jvm") {
            extensions.configure<JavaPluginExtension> { withSourcesJar() }
            extensions.configure<PublishingExtension> {
                publications { create<MavenPublication>("library") { from(components["java"]) } }
                repositories { maven { name = "sdk"; url = uri(koralRepo ?: layout.buildDirectory.dir("repo").get().asFile.path) } }
            }
        }
    }
}
