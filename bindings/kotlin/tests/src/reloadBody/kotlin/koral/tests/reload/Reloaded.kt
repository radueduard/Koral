package koral.tests.reload

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import koral.compose.*

// Version 1. src/reloadBody and src/reloadShape hold the edits the hot-reload test applies to it: keep the
// lines above `Column` the same in all three, so the state's place in the composition is the same.
@Composable
fun Reloaded() {
    var taps by remember { mutableStateOf(0) }
    Column {
        Box(Modifier.size(16.dp).background(Color.Magenta))
        Box(Modifier.size(32.dp).background(if (taps == 0) Color.Red else Color.Green).clickable { taps++ })
    }
}
