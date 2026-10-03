// Koral from Kotlin: the JVM, over koral_c.h and koralUI_c.h through java.lang.foreign — no JNI, no glue
// library. The interfaces are written in Jetpack Compose, on Compose's own runtime.
pluginManagement {
    repositories { gradlePluginPortal(); mavenCentral() }
}
dependencyResolutionManagement {
    repositories { mavenCentral(); google() }
}
rootProject.name = "koral-kotlin"
include("koral", "koral-ui", "tests", "samples:counter")
