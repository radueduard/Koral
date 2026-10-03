plugins {
    kotlin("jvm")
    kotlin("plugin.compose")
    application
}

// `./gradlew :samples:counter:run`, with KORAL_SDK (or KORAL_LIBRARY) saying where Koral is.
// `./gradlew :samples:counter:hotRun` runs it with hot reload: save a source file and the edit is applied
// to the running program. On the JetBrains Runtime (KORAL_JBR) any edit; elsewhere, method bodies only.
dependencies {
    implementation(project(":koral-ui"))
}

application {
    mainClass.set("CounterKt")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}

val koralJar = project(":koral").tasks.named<Jar>("jar")
tasks.register<JavaExec>("hotRun") {
    group = "application"
    description = "Runs the sample, applying source edits while it runs"
    dependsOn(koralJar, tasks.classes)
    classpath = sourceSets.main.get().runtimeClasspath
    mainClass.set(application.mainClass)
    jvmArgs("--enable-native-access=ALL-UNNAMED")
    jvmArgumentProviders.add(CommandLineArgumentProvider { listOf("-javaagent:" + koralJar.get().archiveFile.get().asFile) })
    System.getenv("KORAL_JBR")?.takeIf { it.isNotEmpty() }?.let {
        executable("$it/bin/java")
        jvmArgs("-XX:+AllowEnhancedClassRedefinition")
    }
    val wrapper = rootDir.resolve(if (System.getProperty("os.name").startsWith("Windows")) "gradlew.bat" else "gradlew")
    systemProperty("koral.hotReload", "true")
    systemProperty("koral.hotReload.sources", file("src/main/kotlin").absolutePath)
    systemProperty("koral.hotReload.compile", "\"$wrapper\" -p \"$rootDir\" -q --offline ${project.path}:classes")
}
