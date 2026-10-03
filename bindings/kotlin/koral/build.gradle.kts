plugins { kotlin("jvm") }

// Koral itself: the application, scenes, windows, input, time, resources, commands and the frame graph.
dependencies {
    implementation(kotlin("stdlib"))
    api("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.11.0")
}

// The jar is also the JVM agent hot reload redefines classes through: -javaagent:koral.jar.
tasks.jar {
    manifest {
        attributes(
            "Premain-Class" to "koral.HotReload",
            "Agent-Class" to "koral.HotReload",
            "Can-Redefine-Classes" to "true",
            "Can-Retransform-Classes" to "true",
        )
    }
}
