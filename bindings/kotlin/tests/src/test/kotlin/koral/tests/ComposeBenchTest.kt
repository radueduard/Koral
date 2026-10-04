package koral.tests

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import koral.OffscreenSettings
import koral.Scene
import koral.compose.Alignment
import koral.compose.Arrangement
import koral.compose.Box
import koral.compose.Color
import koral.compose.Column
import koral.compose.ComposeUi
import koral.compose.Modifier
import koral.compose.RoundedCornerShape
import koral.compose.Row
import koral.compose.Text
import koral.compose.UiPass
import koral.compose.background
import koral.compose.dp
import koral.compose.fillMaxSize
import koral.compose.rememberScrollState
import koral.compose.size
import koral.compose.sp
import koral.compose.verticalScroll
import org.junit.jupiter.api.Assumptions.assumeTrue
import kotlin.math.floor
import kotlin.test.Test

private const val PER_ROW = 20

private fun hue(h: Float): Color {
    val x = (h - floor(h)) * 6f
    fun channel(n: Float): Float { val k = (n + x) % 6f; return 0.9f - 0.9f * 0.6f * maxOf(0f, minOf(k, 4f - k, 1f)) }
    return Color(channel(5f), channel(3f), channel(1f), 1f)
}

/** The grid modules/ui/bench/bench.cpp draws from C++ — a rounded box of a colour with a number in it — from Compose. */
class Grid : Scene() {
    var cells by mutableStateOf(0)
    var phase by mutableStateOf(0f)
    var animate = true
    var updateMs = 0.0
    var split = ComposeUi.Timings()

    override fun initialize() {
        graph.add(Clear(0f, 0f, 0f))
        val ui = ComposeUi { Cells() }
        graph.add(UiPass(ui))
        beforeUpdate {
            if (animate) phase += 0.004f
            val before = System.nanoTime()
            ui.update()
            updateMs = (System.nanoTime() - before) / 1e6
            split = ui.timings
        }
    }

    @Composable
    private fun Cells() {
        val phase = phase
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(3.dp)) {
            for (row in 0 until (cells + PER_ROW - 1) / PER_ROW) {
                Row(horizontalArrangement = Arrangement.spacedBy(3.dp)) {
                    for (i in row * PER_ROW until minOf((row + 1) * PER_ROW, cells)) Cell(i, phase)
                }
            }
        }
    }

    @Composable
    private fun Cell(index: Int, phase: Float) {
        Box(Modifier.size(34.dp, 20.dp).background(hue(index * 0.011f + phase), RoundedCornerShape(5.dp)), contentAlignment = Alignment.Center) {
            Text("$index", fontSize = 9.sp, color = Color.Black, softWrap = false)
        }
    }
}

/**
 * What a frame of a Compose interface costs, at one load after another: set KUI_BENCH to measure, and point
 * KORAL_LIBRARY and KORAL_UI_LIBRARY at a Release build — a Debug one measures the debugging.
 */
class ComposeBenchTest {
    @Test
    fun aGridOfCellsFromCompose(): Unit {
        assumeTrue(System.getenv("KUI_BENCH") != null, "set KUI_BENCH to measure")
        headlessApp().use { app ->
            val scene = app.openOffscreen("Grid", Grid(), OffscreenSettings(width = 1280, height = 720))
            // Long enough for the JVM to have compiled what the loop runs.
            scene.cells = 1000
            app.frames(400)
            val frames = 200
            println("BENCH cells | all changing: interface ms, frame ms (recompose, widgets, native) | unchanged: interface ms, frame ms")
            for (cells in listOf(0, 250, 500, 1000, 2000, 4000)) {
                val line = StringBuilder("BENCH %5d |".format(cells))
                for (animate in listOf(true, false)) {
                    scene.cells = cells
                    scene.animate = animate
                    app.frames(40)
                    var sum = 0.0
                    var recompose = 0.0; var widgets = 0.0; var native = 0.0
                    val begin = System.nanoTime()
                    repeat(frames) {
                        app.frame(); sum += scene.updateMs
                        recompose += scene.split.recomposeMs; widgets += scene.split.widgetsMs; native += scene.split.nativeMs
                    }
                    line.append(" %8.3f %8.3f".format(sum / frames, (System.nanoTime() - begin) / 1e6 / frames))
                    line.append(if (animate) " (%.3f, %.3f, %.3f) |".format(recompose / frames, widgets / frames, native / frames) else " |")
                }
                println(line)
            }
        }
    }
}
