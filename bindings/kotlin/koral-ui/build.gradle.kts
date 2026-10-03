plugins {
    kotlin("jvm")
    kotlin("plugin.compose")
}

// koral-ui: its canvas, and interfaces written in Jetpack Compose — Compose's own runtime and compiler,
// with an Applier that builds kui's widgets.
dependencies {
    api(project(":koral"))
    api("org.jetbrains.compose.runtime:runtime:1.12.1")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.11.0")
}
