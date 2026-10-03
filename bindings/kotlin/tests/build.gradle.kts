plugins {
    kotlin("jvm")
    kotlin("plugin.compose")
}

// Koral from Kotlin, end to end on a real device with no display: each test class in a JVM of its own,
// since an application is one per process. KORAL_LIBRARY (or KORAL_SDK) says where Koral is; KORAL_JBR, a
// JetBrains Runtime, runs them with structural hot reload.

// Edited versions of a test's composable, compiled on their own, for the hot-reload test to apply.
val reloadBody = sourceSets.create("reloadBody")
val reloadShape = sourceSets.create("reloadShape")

dependencies {
    testImplementation(project(":koral-ui"))
    testImplementation(kotlin("test"))
    testImplementation("org.junit.jupiter:junit-jupiter:5.13.4")
    testRuntimeOnly("org.junit.platform:junit-platform-launcher")
    "reloadBodyImplementation"(project(":koral-ui"))
    "reloadShapeImplementation"(project(":koral-ui"))
}

val koralJar = project(":koral").tasks.named<Jar>("jar")

tasks.test {
    useJUnitPlatform()
    forkEvery = 1
    dependsOn(koralJar, reloadBody.classesTaskName, reloadShape.classesTaskName)
    jvmArgumentProviders.add(CommandLineArgumentProvider {
        listOf("--enable-native-access=ALL-UNNAMED", "-javaagent:" + koralJar.get().archiveFile.get().asFile)
    })
    systemProperty("koral.test.reloadBody", reloadBody.output.classesDirs.asPath)
    systemProperty("koral.test.reloadShape", reloadShape.output.classesDirs.asPath)
    System.getenv("KORAL_JBR")?.takeIf { it.isNotEmpty() }?.let {
        executable = "$it/bin/java"
        jvmArgs("-XX:+AllowEnhancedClassRedefinition")
    }
    testLogging {
        events("passed", "failed", "skipped", "standard_error", "standard_out")
        showStandardStreams = System.getenv("KORAL_TEST_OUTPUT") != null
        exceptionFormat = org.gradle.api.tasks.testing.logging.TestExceptionFormat.FULL
    }
}
