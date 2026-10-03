// An interface in Kotlin, written in Jetpack Compose and drawn by koral-ui: state, trailing lambdas and
// modifiers, as on Android. `./gradlew :samples:counter:run`

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import koral.App
import koral.Log
import koral.Scene
import koral.WindowSettings
import koral.compose.*

@Composable
fun Counter() {
    var count by remember { mutableStateOf(0) }
    var subtitles by remember { mutableStateOf(true) }
    var volume by remember { mutableStateOf(0.6f) }
    val theme = LocalTheme.current

    Box(Modifier.fillMaxSize().background(theme.background), contentAlignment = Alignment.Center) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(40.dp)) {
            Badge(count)
            Column(
                Modifier.width(320.dp).background(theme.surface, RoundedCornerShape(12.dp))
                    .border(1.dp, theme.border, RoundedCornerShape(12.dp)).padding(20.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                Text("Counter", fontSize = 22.sp)
                Text("Clicked $count times", color = theme.textMuted)
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Text("Subtitles")
                    Spacer(Modifier.weight(1f))
                    Switch(subtitles, { subtitles = it })
                }
                Text("Volume ${(volume * 100).toInt()}%")
                Slider(volume, { volume = it }, Modifier.fillMaxWidth())
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedButton(onClick = { count = 0 }) { Text("Reset") }
                    Spacer(Modifier.weight(1f))
                    Button(onClick = { count++; Log.info("[counter] $count") }) { Text("Add one") }
                }
            }
        }
    }
}

/** A canvas: a ring that fills a little more with every click. */
@Composable
fun Badge(count: Int) = Canvas(Modifier.size(200.dp)) {
    drawCircle(Color(0xFF2B2F6B))
    drawCircle(Color(0xFF7090FF), radius = size.minDimension / 2f - 2f, style = Stroke(width = 3f))
    drawArc(Color.White, -90f, (count % 12 + 1) * 30f, useCenter = false,
            topLeft = Offset(30f, 30f), size = Size(140f, 140f), style = Stroke(width = 10f, cap = StrokeCap.Round))
}

class CounterScene : Scene() {
    override fun initialize() { setContent { Counter() } }
}

fun main() {
    App().use { app ->
        app.register("Counter") { CounterScene() }
        app.open("Counter", WindowSettings(title = "Koral — Compose counter", width = 900, height = 520))
        app.run()
    }
}
