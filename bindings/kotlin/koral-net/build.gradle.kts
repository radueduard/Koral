plugins { kotlin("jvm") }

// koral-net: sockets, HTTP and WebSockets as suspend functions, the game protocol, replication and prediction.
dependencies {
    api(project(":koral"))
}
