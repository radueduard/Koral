package koral.compose

import androidx.compose.runtime.Composable
import kotlin.math.roundToInt

// The controls that change a number, for every kind of number an interface is written in: a Slider and a
// DragValue take a Float, and here an Int, a Double, a Dp and a TextUnit (sp) — each handed back as what it
// was given, so that a size kept as a Dp is set by a slider without being taken apart and put together.
//
//     var radius by remember { mutableStateOf(10.dp) }
//     Slider(radius, { radius = it }, valueRange = 0.dp..24.dp)
//     DragValue(radius, { radius = it }, label = "Radius", valueRange = 0.dp..24.dp)

// ---- sliders ------------------------------------------------------------------------------------------------

/** A [Slider] over whole numbers: it stops only at them. */
@Composable
fun Slider(value: Int, onValueChange: (Int) -> Unit, modifier: Modifier = Modifier, valueRange: IntRange = 0..100) =
    Slider(value.toFloat(), { onValueChange(it.roundToInt().coerceIn(valueRange.first, valueRange.last)) }, modifier,
           valueRange = valueRange.first.toFloat()..valueRange.last.toFloat())

/** A [Slider] over a Double. */
@Composable
fun Slider(value: Double, onValueChange: (Double) -> Unit, modifier: Modifier = Modifier, valueRange: ClosedFloatingPointRange<Double> = 0.0..1.0) =
    Slider(value.toFloat(), { onValueChange(it.toDouble()) }, modifier, valueRange = valueRange.start.toFloat()..valueRange.endInclusive.toFloat())

/** A [Slider] over a length: `Slider(radius, { radius = it }, valueRange = 0.dp..24.dp)`. */
@Composable
fun Slider(value: Dp, onValueChange: (Dp) -> Unit, modifier: Modifier = Modifier, valueRange: ClosedRange<Dp> = 0.dp..100.dp) =
    Slider(value.value, { onValueChange(it.dp) }, modifier, valueRange = valueRange.start.value..valueRange.endInclusive.value)

/** A [Slider] over a text size: `Slider(size, { size = it }, valueRange = 8.sp..32.sp)`. */
@Composable
fun Slider(value: TextUnit, onValueChange: (TextUnit) -> Unit, modifier: Modifier = Modifier, valueRange: ClosedRange<TextUnit> = 8.sp..48.sp) =
    Slider(value.value, { onValueChange(it.sp) }, modifier, valueRange = valueRange.start.value..valueRange.endInclusive.value)

// ---- drag values --------------------------------------------------------------------------------------------

/** A [DragValue] over whole numbers. [speed] is how many a unit of dragging adds. */
@Composable
fun DragValue(value: Int, onValueChange: (Int) -> Unit, modifier: Modifier = Modifier, label: String = "", speed: Float = 0.1f,
              valueRange: IntRange = Int.MIN_VALUE..Int.MAX_VALUE, width: Dp = Dp.Unspecified) =
    DragValue(value.toFloat(), { onValueChange(it.roundToInt().coerceIn(valueRange.first, valueRange.last)) }, modifier, label, speed,
              valueRange.first.toFloat()..valueRange.last.toFloat(), decimals = 0, width = width)

/** A [DragValue] over a Double. */
@Composable
fun DragValue(value: Double, onValueChange: (Double) -> Unit, modifier: Modifier = Modifier, label: String = "", speed: Float = 0.01f,
              valueRange: ClosedFloatingPointRange<Double> = Double.NEGATIVE_INFINITY..Double.POSITIVE_INFINITY, decimals: Int = 2,
              width: Dp = Dp.Unspecified) =
    DragValue(value.toFloat(), { onValueChange(it.toDouble()) }, modifier, label, speed, valueRange.start.toFloat()..valueRange.endInclusive.toFloat(),
              decimals, width)

/** A [DragValue] over a length: `DragValue(gap, { gap = it }, label = "Gap", valueRange = 0.dp..24.dp)`. */
@Composable
fun DragValue(value: Dp, onValueChange: (Dp) -> Unit, modifier: Modifier = Modifier, label: String = "", speed: Float = 0.1f,
              valueRange: ClosedRange<Dp> = Dp(Float.NEGATIVE_INFINITY)..Dp(Float.POSITIVE_INFINITY), decimals: Int = 0,
              width: Dp = Dp.Unspecified) =
    DragValue(value.value, { onValueChange(it.dp) }, modifier, label, speed, valueRange.start.value..valueRange.endInclusive.value, decimals, width)

/** A [DragValue] over a text size: `DragValue(size, { size = it }, label = "Text", valueRange = 8.sp..32.sp)`. */
@Composable
fun DragValue(value: TextUnit, onValueChange: (TextUnit) -> Unit, modifier: Modifier = Modifier, label: String = "", speed: Float = 0.1f,
              valueRange: ClosedRange<TextUnit> = TextUnit(Float.NEGATIVE_INFINITY)..TextUnit(Float.POSITIVE_INFINITY), decimals: Int = 0,
              width: Dp = Dp.Unspecified) =
    DragValue(value.value, { onValueChange(it.sp) }, modifier, label, speed, valueRange.start.value..valueRange.endInclusive.value, decimals, width)

// ---- progress -----------------------------------------------------------------------------------------------

/** A bar filled to [value] of [range]: how far along a count, a length or a size is. */
@Composable
fun LinearProgressIndicator(value: Int, range: IntRange, modifier: Modifier = Modifier) =
    LinearProgressIndicator({ if (range.last > range.first) (value - range.first).toFloat() / (range.last - range.first) else 0f }, modifier)

// ---- vectors ------------------------------------------------------------------------------------------------
//
// A vector is its components, each the control its kind of number has: a DragValue of a Vec3 is three DragValues
// side by side, a Slider of one three Sliders one over the other — and what comes back is the vector, with the
// one component changed.
//
//     var position by remember { mutableStateOf(Vec3(0f, 0.5f, 0f)) }
//     DragValue(position, { position = it }, speed = 0.02f)
//     Slider(tint, { tint = it })                                   // a Vec4: four sliders, 0 to 1

private val Axes = listOf("X", "Y", "Z", "W")

@Composable
private fun DragFloats(values: List<Float>, onChange: (List<Float>) -> Unit, modifier: Modifier, labels: List<String>, speed: Float,
                       range: ClosedFloatingPointRange<Float>, decimals: Int, width: Dp) =
    Row(modifier, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        values.forEachIndexed { i, component ->
            DragValue(component, { v -> onChange(values.toMutableList().also { it[i] = v }) }, label = labels.getOrElse(i) { "" }, speed = speed,
                      valueRange = range, decimals = decimals, width = width)
        }
    }

@Composable
private fun SlideFloats(values: List<Float>, onChange: (List<Float>) -> Unit, modifier: Modifier, range: ClosedFloatingPointRange<Float>) =
    Column(modifier, verticalArrangement = Arrangement.spacedBy(4.dp)) {
        values.forEachIndexed { i, component -> Slider(component, { v -> onChange(values.toMutableList().also { it[i] = v }) }, valueRange = range) }
    }

private val Unbounded = Float.NEGATIVE_INFINITY..Float.POSITIVE_INFINITY
private fun IntRange.floats() = first.toFloat()..last.toFloat()
private fun List<Float>.whole(range: IntRange) = map { it.roundToInt().coerceIn(range.first, range.last) }

/** A [DragValue] a component, side by side. [labels] are written before each; [width] is each one's. */
@Composable
fun DragValue(value: koral.Vec2, onValueChange: (koral.Vec2) -> Unit, modifier: Modifier = Modifier, labels: List<String> = Axes, speed: Float = 0.01f,
              valueRange: ClosedFloatingPointRange<Float> = Unbounded, decimals: Int = 2, width: Dp = 96.dp) =
    DragFloats(listOf(value.x, value.y), { onValueChange(koral.Vec2(it[0], it[1])) }, modifier, labels, speed, valueRange, decimals, width)

@Composable
fun DragValue(value: koral.Vec3, onValueChange: (koral.Vec3) -> Unit, modifier: Modifier = Modifier, labels: List<String> = Axes, speed: Float = 0.01f,
              valueRange: ClosedFloatingPointRange<Float> = Unbounded, decimals: Int = 2, width: Dp = 96.dp) =
    DragFloats(listOf(value.x, value.y, value.z), { onValueChange(koral.Vec3(it[0], it[1], it[2])) }, modifier, labels, speed, valueRange, decimals, width)

@Composable
fun DragValue(value: koral.Vec4, onValueChange: (koral.Vec4) -> Unit, modifier: Modifier = Modifier, labels: List<String> = Axes, speed: Float = 0.01f,
              valueRange: ClosedFloatingPointRange<Float> = Unbounded, decimals: Int = 2, width: Dp = 96.dp) =
    DragFloats(listOf(value.x, value.y, value.z, value.w), { onValueChange(koral.Vec4(it[0], it[1], it[2], it[3])) }, modifier, labels, speed, valueRange,
               decimals, width)

/** Whole numbers a component. */
@Composable
fun DragValue(value: koral.IVec2, onValueChange: (koral.IVec2) -> Unit, modifier: Modifier = Modifier, labels: List<String> = Axes, speed: Float = 0.1f,
              valueRange: IntRange = Int.MIN_VALUE..Int.MAX_VALUE, width: Dp = 96.dp) =
    DragFloats(listOf(value.x.toFloat(), value.y.toFloat()), { val v = it.whole(valueRange); onValueChange(koral.IVec2(v[0], v[1])) }, modifier, labels, speed,
               valueRange.floats(), 0, width)

@Composable
fun DragValue(value: koral.IVec3, onValueChange: (koral.IVec3) -> Unit, modifier: Modifier = Modifier, labels: List<String> = Axes, speed: Float = 0.1f,
              valueRange: IntRange = Int.MIN_VALUE..Int.MAX_VALUE, width: Dp = 96.dp) =
    DragFloats(listOf(value.x.toFloat(), value.y.toFloat(), value.z.toFloat()), { val v = it.whole(valueRange); onValueChange(koral.IVec3(v[0], v[1], v[2])) },
               modifier, labels, speed, valueRange.floats(), 0, width)

@Composable
fun DragValue(value: koral.IVec4, onValueChange: (koral.IVec4) -> Unit, modifier: Modifier = Modifier, labels: List<String> = Axes, speed: Float = 0.1f,
              valueRange: IntRange = Int.MIN_VALUE..Int.MAX_VALUE, width: Dp = 96.dp) =
    DragFloats(listOf(value.x.toFloat(), value.y.toFloat(), value.z.toFloat(), value.w.toFloat()),
               { val v = it.whole(valueRange); onValueChange(koral.IVec4(v[0], v[1], v[2], v[3])) }, modifier, labels, speed, valueRange.floats(), 0, width)

/** Whole numbers a component, none below zero. */
@Composable
fun DragValue(value: koral.UVec2, onValueChange: (koral.UVec2) -> Unit, modifier: Modifier = Modifier, labels: List<String> = Axes, speed: Float = 0.1f,
              valueRange: IntRange = 0..Int.MAX_VALUE, width: Dp = 96.dp) =
    DragFloats(listOf(value.x.toFloat(), value.y.toFloat()), { val v = it.whole(valueRange); onValueChange(koral.UVec2(v[0], v[1])) }, modifier, labels, speed,
               valueRange.floats(), 0, width)

@Composable
fun DragValue(value: koral.UVec3, onValueChange: (koral.UVec3) -> Unit, modifier: Modifier = Modifier, labels: List<String> = Axes, speed: Float = 0.1f,
              valueRange: IntRange = 0..Int.MAX_VALUE, width: Dp = 96.dp) =
    DragFloats(listOf(value.x.toFloat(), value.y.toFloat(), value.z.toFloat()), { val v = it.whole(valueRange); onValueChange(koral.UVec3(v[0], v[1], v[2])) },
               modifier, labels, speed, valueRange.floats(), 0, width)

@Composable
fun DragValue(value: koral.UVec4, onValueChange: (koral.UVec4) -> Unit, modifier: Modifier = Modifier, labels: List<String> = Axes, speed: Float = 0.1f,
              valueRange: IntRange = 0..Int.MAX_VALUE, width: Dp = 96.dp) =
    DragFloats(listOf(value.x.toFloat(), value.y.toFloat(), value.z.toFloat(), value.w.toFloat()),
               { val v = it.whole(valueRange); onValueChange(koral.UVec4(v[0], v[1], v[2], v[3])) }, modifier, labels, speed, valueRange.floats(), 0, width)

/** A point and a size of the interface's own: two numbers each. */
@Composable
fun DragValue(value: Offset, onValueChange: (Offset) -> Unit, modifier: Modifier = Modifier, labels: List<String> = Axes, speed: Float = 0.1f,
              valueRange: ClosedFloatingPointRange<Float> = Unbounded, decimals: Int = 1, width: Dp = 96.dp) =
    DragFloats(listOf(value.x, value.y), { onValueChange(Offset(it[0], it[1])) }, modifier, labels, speed, valueRange, decimals, width)

@Composable
fun DragValue(value: Size, onValueChange: (Size) -> Unit, modifier: Modifier = Modifier, labels: List<String> = listOf("W", "H"), speed: Float = 0.1f,
              valueRange: ClosedFloatingPointRange<Float> = 0f..Float.POSITIVE_INFINITY, decimals: Int = 1, width: Dp = 96.dp) =
    DragFloats(listOf(value.width, value.height), { onValueChange(Size(it[0], it[1])) }, modifier, labels, speed, valueRange, decimals, width)

/** A [Slider] a component, one over the other, each over [valueRange]. */
@Composable
fun Slider(value: koral.Vec2, onValueChange: (koral.Vec2) -> Unit, modifier: Modifier = Modifier, valueRange: ClosedFloatingPointRange<Float> = 0f..1f) =
    SlideFloats(listOf(value.x, value.y), { onValueChange(koral.Vec2(it[0], it[1])) }, modifier, valueRange)

@Composable
fun Slider(value: koral.Vec3, onValueChange: (koral.Vec3) -> Unit, modifier: Modifier = Modifier, valueRange: ClosedFloatingPointRange<Float> = 0f..1f) =
    SlideFloats(listOf(value.x, value.y, value.z), { onValueChange(koral.Vec3(it[0], it[1], it[2])) }, modifier, valueRange)

@Composable
fun Slider(value: koral.Vec4, onValueChange: (koral.Vec4) -> Unit, modifier: Modifier = Modifier, valueRange: ClosedFloatingPointRange<Float> = 0f..1f) =
    SlideFloats(listOf(value.x, value.y, value.z, value.w), { onValueChange(koral.Vec4(it[0], it[1], it[2], it[3])) }, modifier, valueRange)

@Composable
fun Slider(value: koral.IVec2, onValueChange: (koral.IVec2) -> Unit, modifier: Modifier = Modifier, valueRange: IntRange = 0..100) =
    SlideFloats(listOf(value.x.toFloat(), value.y.toFloat()), { val v = it.whole(valueRange); onValueChange(koral.IVec2(v[0], v[1])) }, modifier, valueRange.floats())

@Composable
fun Slider(value: koral.IVec3, onValueChange: (koral.IVec3) -> Unit, modifier: Modifier = Modifier, valueRange: IntRange = 0..100) =
    SlideFloats(listOf(value.x.toFloat(), value.y.toFloat(), value.z.toFloat()), { val v = it.whole(valueRange); onValueChange(koral.IVec3(v[0], v[1], v[2])) },
                modifier, valueRange.floats())

@Composable
fun Slider(value: koral.IVec4, onValueChange: (koral.IVec4) -> Unit, modifier: Modifier = Modifier, valueRange: IntRange = 0..100) =
    SlideFloats(listOf(value.x.toFloat(), value.y.toFloat(), value.z.toFloat(), value.w.toFloat()),
                { val v = it.whole(valueRange); onValueChange(koral.IVec4(v[0], v[1], v[2], v[3])) }, modifier, valueRange.floats())

@Composable
fun Slider(value: koral.UVec2, onValueChange: (koral.UVec2) -> Unit, modifier: Modifier = Modifier, valueRange: IntRange = 0..100) =
    SlideFloats(listOf(value.x.toFloat(), value.y.toFloat()), { val v = it.whole(valueRange); onValueChange(koral.UVec2(v[0], v[1])) }, modifier, valueRange.floats())

@Composable
fun Slider(value: koral.UVec3, onValueChange: (koral.UVec3) -> Unit, modifier: Modifier = Modifier, valueRange: IntRange = 0..100) =
    SlideFloats(listOf(value.x.toFloat(), value.y.toFloat(), value.z.toFloat()), { val v = it.whole(valueRange); onValueChange(koral.UVec3(v[0], v[1], v[2])) },
                modifier, valueRange.floats())

@Composable
fun Slider(value: koral.UVec4, onValueChange: (koral.UVec4) -> Unit, modifier: Modifier = Modifier, valueRange: IntRange = 0..100) =
    SlideFloats(listOf(value.x.toFloat(), value.y.toFloat(), value.z.toFloat(), value.w.toFloat()),
                { val v = it.whole(valueRange); onValueChange(koral.UVec4(v[0], v[1], v[2], v[3])) }, modifier, valueRange.floats())
