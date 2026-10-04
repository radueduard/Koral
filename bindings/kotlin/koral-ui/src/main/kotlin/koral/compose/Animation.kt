package koral.compose

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.State
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import kotlin.math.abs

// Animation, as Compose's animation-core writes it: animate*AsState, Animatable, tween / spring / snap,
// AnimatedVisibility, Crossfade and an infinite transition — driven by the interface's own frames.
//
//     val alpha by animateFloatAsState(if (shown) 1f else 0f)
//     val width by animateDpAsState(if (open) 240.dp else 48.dp, tween(200))

// ---- easing and specs -----------------------------------------------------------------------------------------

fun interface Easing { fun transform(fraction: Float): Float }

/** A cubic Bézier from (0, 0) to (1, 1) through (a, b) and (c, d): y for the x that is the fraction. */
class CubicBezierEasing(private val a: Float, private val b: Float, private val c: Float, private val d: Float) : Easing {
    private fun at(t: Float, p1: Float, p2: Float): Float { val u = 1f - t; return 3f * u * u * t * p1 + 3f * u * t * t * p2 + t * t * t }
    override fun transform(fraction: Float): Float {
        if (fraction <= 0f) return 0f
        if (fraction >= 1f) return 1f
        var low = 0f; var high = 1f
        repeat(20) { val mid = (low + high) / 2f; if (at(mid, a, c) < fraction) low = mid else high = mid }
        return at((low + high) / 2f, b, d)
    }
}

val LinearEasing = Easing { it }
val FastOutSlowInEasing: Easing = CubicBezierEasing(0.4f, 0f, 0.2f, 1f)
val LinearOutSlowInEasing: Easing = CubicBezierEasing(0f, 0f, 0.2f, 1f)
val FastOutLinearInEasing: Easing = CubicBezierEasing(0.4f, 0f, 1f, 1f)
val EaseIn: Easing = CubicBezierEasing(0.42f, 0f, 1f, 1f)
val EaseOut: Easing = CubicBezierEasing(0f, 0f, 0.58f, 1f)
val EaseInOut: Easing = CubicBezierEasing(0.42f, 0f, 0.58f, 1f)

/** How a value gets from where it is to where it is going. */
sealed interface AnimationSpec<T>
sealed interface FiniteAnimationSpec<T> : AnimationSpec<T>
sealed interface DurationBasedAnimationSpec<T> : FiniteAnimationSpec<T>
class TweenSpec<T>(val durationMillis: Int = 300, val delay: Int = 0, val easing: Easing = FastOutSlowInEasing) : DurationBasedAnimationSpec<T>
class SpringSpec<T>(val dampingRatio: Float = Spring.DampingRatioNoBouncy, val stiffness: Float = Spring.StiffnessMedium,
                    val visibilityThreshold: T? = null) : FiniteAnimationSpec<T>
class SnapSpec<T>(val delay: Int = 0) : DurationBasedAnimationSpec<T>
enum class RepeatMode { Restart, Reverse }
class InfiniteRepeatableSpec<T>(val animation: DurationBasedAnimationSpec<T>, val repeatMode: RepeatMode = RepeatMode.Restart) : AnimationSpec<T>

object Spring {
    const val StiffnessHigh = 10_000f
    const val StiffnessMedium = 1500f
    const val StiffnessMediumLow = 400f
    const val StiffnessLow = 200f
    const val StiffnessVeryLow = 50f
    const val DampingRatioHighBouncy = 0.2f
    const val DampingRatioMediumBouncy = 0.5f
    const val DampingRatioLowBouncy = 0.75f
    const val DampingRatioNoBouncy = 1f
}

fun <T> tween(durationMillis: Int = 300, delayMillis: Int = 0, easing: Easing = FastOutSlowInEasing): TweenSpec<T> = TweenSpec(durationMillis, delayMillis, easing)
fun <T> spring(dampingRatio: Float = Spring.DampingRatioNoBouncy, stiffness: Float = Spring.StiffnessMedium, visibilityThreshold: T? = null): SpringSpec<T> =
    SpringSpec(dampingRatio, stiffness, visibilityThreshold)
fun <T> snap(delayMillis: Int = 0): SnapSpec<T> = SnapSpec(delayMillis)
fun <T> infiniteRepeatable(animation: DurationBasedAnimationSpec<T>, repeatMode: RepeatMode = RepeatMode.Restart): InfiniteRepeatableSpec<T> =
    InfiniteRepeatableSpec(animation, repeatMode)

// ---- what can be animated: anything that is so many numbers ----------------------------------------------------

/** A value as numbers, and back: what lets it be animated a number at a time. */
class TwoWayConverter<T>(val convertToVector: (T) -> FloatArray, val convertFromVector: (FloatArray) -> T)

object VectorConverters {
    val Float = TwoWayConverter<Float>({ floatArrayOf(it) }, { it[0] })
    val Int = TwoWayConverter<Int>({ floatArrayOf(it.toFloat()) }, { Math.round(it[0]) })
    val Dp = TwoWayConverter<Dp>({ floatArrayOf(it.value) }, { Dp(it[0]) })
    val Color = TwoWayConverter<Color>({ floatArrayOf(it.red, it.green, it.blue, it.alpha) }, { Color(it[0], it[1], it[2], it[3]) })
    val Offset = TwoWayConverter<Offset>({ floatArrayOf(it.x, it.y) }, { Offset(it[0], it[1]) })
    val Size = TwoWayConverter<Size>({ floatArrayOf(it.width, it.height) }, { Size(it[0], it[1]) })
}

/**
 * A value that is animated from wherever it is: [animateTo] takes it to a target over frames and returns when it
 * is there, [snapTo] puts it there at once. Read [value] where it is shown; what reads it is drawn again as it moves.
 */
class Animatable<T>(initialValue: T, private val converter: TwoWayConverter<T>) {
    var value: T by mutableStateOf(initialValue)
        private set
    var targetValue: T = initialValue
        private set
    var isRunning: Boolean by mutableStateOf(false)
        private set
    private var velocity = FloatArray(converter.convertToVector(initialValue).size)
    private var run = 0

    fun asState(): State<T> = object : State<T> { override val value: T get() = this@Animatable.value }

    suspend fun snapTo(targetValue: T) {
        run++
        this.targetValue = targetValue
        value = targetValue
        velocity.fill(0f)
        isRunning = false
    }

    suspend fun animateTo(targetValue: T, animationSpec: AnimationSpec<T> = spring()) {
        val mine = ++run     // a later call takes over: this one stops where it has got to
        this.targetValue = targetValue
        isRunning = true
        try {
            when (animationSpec) {
                is SnapSpec -> { wait(animationSpec.delay, mine); if (mine == run) value = targetValue }
                is TweenSpec -> tween(targetValue, animationSpec, mine)
                is SpringSpec -> spring(targetValue, animationSpec, mine)
                is InfiniteRepeatableSpec -> {
                    val from = value
                    var forward = true
                    while (mine == run) {
                        if (animationSpec.repeatMode == RepeatMode.Restart) value = from
                        val to = if (forward) targetValue else from
                        when (val each = animationSpec.animation) {
                            is TweenSpec -> tween(to, each, mine)
                            is SnapSpec -> { wait(each.delay, mine); value = to }
                        }
                        if (animationSpec.repeatMode == RepeatMode.Reverse) forward = !forward
                    }
                }
            }
        } finally {
            if (mine == run) isRunning = false
        }
    }

    private suspend fun wait(millis: Int, mine: Int) {
        if (millis <= 0) return
        val start = withFrameNanos { it }
        while (mine == run && withFrameNanos { it } - start < millis * 1_000_000L) { /* a frame at a time */ }
    }

    private suspend fun tween(target: T, spec: TweenSpec<T>, mine: Int) {
        wait(spec.delay, mine)
        val from = converter.convertToVector(value)
        val to = converter.convertToVector(target)
        val start = withFrameNanos { it }
        val duration = maxOf(spec.durationMillis, 1) * 1_000_000L
        while (mine == run) {
            val fraction = ((withFrameNanos { it } - start).toFloat() / duration).coerceIn(0f, 1f)
            val eased = spec.easing.transform(fraction)
            if (mine != run) return
            value = converter.convertFromVector(FloatArray(from.size) { from[it] + (to[it] - from[it]) * eased })
            if (fraction >= 1f) break
        }
        velocity.fill(0f)
    }

    private suspend fun spring(target: T, spec: SpringSpec<T>, mine: Int) {
        val to = converter.convertToVector(target)
        val at = converter.convertToVector(value)
        if (velocity.size != at.size) velocity = FloatArray(at.size)
        val damping = 2f * spec.dampingRatio * kotlin.math.sqrt(spec.stiffness)
        // Near enough: a hundredth of the way, at the least a thousandth of a unit.
        val near = FloatArray(at.size) { maxOf(abs(to[it] - at[it]) * 0.005f, 0.001f) }
        var last = withFrameNanos { it }
        while (mine == run) {
            val now = withFrameNanos { it }
            var left = ((now - last) / 1e9f).coerceIn(0f, 0.05f)
            last = now
            // In small steps: a stiff spring taken a frame at a time would fly apart.
            while (left > 0f) {
                val dt = minOf(left, 0.002f)
                for (i in at.indices) {
                    velocity[i] += (-spec.stiffness * (at[i] - to[i]) - damping * velocity[i]) * dt
                    at[i] += velocity[i] * dt
                }
                left -= dt
            }
            if (mine != run) return
            val settled = at.indices.all { abs(at[it] - to[it]) < near[it] && abs(velocity[it]) < near[it] * 20f }
            value = if (settled) target else converter.convertFromVector(at.copyOf())
            if (settled) { velocity.fill(0f); break }
        }
    }
}

fun Animatable(initialValue: Float): Animatable<Float> = Animatable(initialValue, VectorConverters.Float)

// ---- animate*AsState ------------------------------------------------------------------------------------------

@Composable
fun <T> animateValueAsState(targetValue: T, typeConverter: TwoWayConverter<T>, animationSpec: AnimationSpec<T> = spring(),
                            @Suppress("UNUSED_PARAMETER") label: String = "ValueAnimation", finishedListener: ((T) -> Unit)? = null): State<T> {
    val animatable = remember { Animatable(targetValue, typeConverter) }
    val spec by rememberUpdatedState(animationSpec)
    val finished by rememberUpdatedState(finishedListener)
    LaunchedEffect(targetValue) {
        if (animatable.value != targetValue || animatable.targetValue != targetValue) {
            animatable.animateTo(targetValue, spec)
            finished?.invoke(animatable.value)
        }
    }
    return remember { animatable.asState() }
}

@Composable
fun animateFloatAsState(targetValue: Float, animationSpec: AnimationSpec<Float> = spring(), label: String = "FloatAnimation",
                        finishedListener: ((Float) -> Unit)? = null): State<Float> =
    animateValueAsState(targetValue, VectorConverters.Float, animationSpec, label, finishedListener)

@Composable
fun animateIntAsState(targetValue: Int, animationSpec: AnimationSpec<Int> = spring(), label: String = "IntAnimation",
                      finishedListener: ((Int) -> Unit)? = null): State<Int> =
    animateValueAsState(targetValue, VectorConverters.Int, animationSpec, label, finishedListener)

@Composable
fun animateDpAsState(targetValue: Dp, animationSpec: AnimationSpec<Dp> = spring(), label: String = "DpAnimation",
                     finishedListener: ((Dp) -> Unit)? = null): State<Dp> =
    animateValueAsState(targetValue, VectorConverters.Dp, animationSpec, label, finishedListener)

@Composable
fun animateColorAsState(targetValue: Color, animationSpec: AnimationSpec<Color> = spring(), label: String = "ColorAnimation",
                        finishedListener: ((Color) -> Unit)? = null): State<Color> =
    animateValueAsState(targetValue, VectorConverters.Color, animationSpec, label, finishedListener)

@Composable
fun animateOffsetAsState(targetValue: Offset, animationSpec: AnimationSpec<Offset> = spring(), label: String = "OffsetAnimation",
                         finishedListener: ((Offset) -> Unit)? = null): State<Offset> =
    animateValueAsState(targetValue, VectorConverters.Offset, animationSpec, label, finishedListener)

@Composable
fun animateSizeAsState(targetValue: Size, animationSpec: AnimationSpec<Size> = spring(), label: String = "SizeAnimation",
                       finishedListener: ((Size) -> Unit)? = null): State<Size> =
    animateValueAsState(targetValue, VectorConverters.Size, animationSpec, label, finishedListener)

// ---- infinite transitions -------------------------------------------------------------------------------------

class InfiniteTransition internal constructor()

@Composable
fun rememberInfiniteTransition(@Suppress("UNUSED_PARAMETER") label: String = "InfiniteTransition"): InfiniteTransition = remember { InfiniteTransition() }

/** A number going from [initialValue] to [targetValue] and round again for as long as it is composed. */
@Composable
fun InfiniteTransition.animateFloat(initialValue: Float, targetValue: Float, animationSpec: InfiniteRepeatableSpec<Float>,
                                    @Suppress("UNUSED_PARAMETER") label: String = "FloatAnimation"): State<Float> {
    val animatable = remember { Animatable(initialValue) }
    LaunchedEffect(initialValue, targetValue) {
        animatable.snapTo(initialValue)
        animatable.animateTo(targetValue, animationSpec)
    }
    return remember { animatable.asState() }
}

// ---- visibility -----------------------------------------------------------------------------------------------

/** What a transition does to what it brings in or takes out: each part is null where it does nothing. */
internal data class Effects(val alpha: Float? = null, val scale: Float? = null, val growWidth: Boolean = false, val growHeight: Boolean = false,
                            val slideX: ((Int) -> Int)? = null, val slideY: ((Int) -> Int)? = null) {
    operator fun plus(o: Effects) = Effects(alpha ?: o.alpha, scale ?: o.scale, growWidth || o.growWidth, growHeight || o.growHeight,
                                            slideX ?: o.slideX, slideY ?: o.slideY)
}

/** How something comes into view: fading, growing, sliding, scaling — `fadeIn() + expandVertically()` is both at once. */
class EnterTransition internal constructor(internal val spec: AnimationSpec<Float>, internal val effects: Effects = Effects()) {
    operator fun plus(enter: EnterTransition): EnterTransition = EnterTransition(spec, effects + enter.effects)
    companion object { val None = EnterTransition(snap()) }
}

/** How something goes out of view: an [EnterTransition] run backwards. */
class ExitTransition internal constructor(internal val spec: AnimationSpec<Float>, internal val effects: Effects = Effects()) {
    operator fun plus(exit: ExitTransition): ExitTransition = ExitTransition(spec, effects + exit.effects)
    companion object { val None = ExitTransition(snap()) }
}

private val Gentle: AnimationSpec<Float> get() = spring(stiffness = Spring.StiffnessMediumLow)

fun fadeIn(animationSpec: AnimationSpec<Float> = Gentle, initialAlpha: Float = 0f) = EnterTransition(animationSpec, Effects(alpha = initialAlpha))
fun fadeOut(animationSpec: AnimationSpec<Float> = Gentle, targetAlpha: Float = 0f) = ExitTransition(animationSpec, Effects(alpha = targetAlpha))
/** Growing from nothing to its height (its width; both), what is not yet in view cut off. */
fun expandVertically(animationSpec: AnimationSpec<Float> = Gentle) = EnterTransition(animationSpec, Effects(growHeight = true))
fun expandHorizontally(animationSpec: AnimationSpec<Float> = Gentle) = EnterTransition(animationSpec, Effects(growWidth = true))
fun expandIn(animationSpec: AnimationSpec<Float> = Gentle) = EnterTransition(animationSpec, Effects(growWidth = true, growHeight = true))
fun shrinkVertically(animationSpec: AnimationSpec<Float> = Gentle) = ExitTransition(animationSpec, Effects(growHeight = true))
fun shrinkHorizontally(animationSpec: AnimationSpec<Float> = Gentle) = ExitTransition(animationSpec, Effects(growWidth = true))
fun shrinkOut(animationSpec: AnimationSpec<Float> = Gentle) = ExitTransition(animationSpec, Effects(growWidth = true, growHeight = true))
/** From [initialScale] of its size, about its middle. */
fun scaleIn(animationSpec: AnimationSpec<Float> = Gentle, initialScale: Float = 0f) = EnterTransition(animationSpec, Effects(scale = initialScale))
fun scaleOut(animationSpec: AnimationSpec<Float> = Gentle, targetScale: Float = 0f) = ExitTransition(animationSpec, Effects(scale = targetScale))
/** From where [initialOffsetY] puts it, given its height: by default half of it higher. */
fun slideInVertically(animationSpec: AnimationSpec<Float> = Gentle, initialOffsetY: (fullHeight: Int) -> Int = { -it / 2 }) =
    EnterTransition(animationSpec, Effects(slideY = initialOffsetY))
fun slideInHorizontally(animationSpec: AnimationSpec<Float> = Gentle, initialOffsetX: (fullWidth: Int) -> Int = { -it / 2 }) =
    EnterTransition(animationSpec, Effects(slideX = initialOffsetX))
fun slideOutVertically(animationSpec: AnimationSpec<Float> = Gentle, targetOffsetY: (fullHeight: Int) -> Int = { -it / 2 }) =
    ExitTransition(animationSpec, Effects(slideY = targetOffsetY))
fun slideOutHorizontally(animationSpec: AnimationSpec<Float> = Gentle, targetOffsetX: (fullWidth: Int) -> Int = { -it / 2 }) =
    ExitTransition(animationSpec, Effects(slideX = targetOffsetX))

interface AnimatedVisibilityScope
private object AnimatedVisibilityScopeInstance : AnimatedVisibilityScope

/**
 * [content] while [visible], coming into view as [enter] says and leaving as [exit] does — as Compose's, fading
 * in and growing, fading out and shrinking, unless told otherwise. It stays composed until it has gone, and
 * takes the room it is drawn in: one that grows pushes what is after it along as it does.
 */
@Composable
fun AnimatedVisibility(visible: Boolean, modifier: Modifier = Modifier, enter: EnterTransition = fadeIn() + expandIn(),
                       exit: ExitTransition = fadeOut() + shrinkOut(),
                       @Suppress("UNUSED_PARAMETER") label: String = "AnimatedVisibility", content: @Composable AnimatedVisibilityScope.() -> Unit) {
    val shown by animateFloatAsState(if (visible) 1f else 0f, if (visible) enter.spec else exit.spec)
    if (!visible && shown <= 0.001f) return
    val effects = if (visible) enter.effects else exit.effects
    val away = 1f - shown
    var all = modifier
    if (effects.growWidth || effects.growHeight) all = all.clipToBounds()
    effects.alpha?.let { all = all.alpha(it + (1f - it) * shown) }
    effects.scale?.let { val s = it + (1f - it) * shown; all = all.graphicsLayer(scaleX = s, scaleY = s) }
    Layout({ Box { AnimatedVisibilityScopeInstance.content() } }, all) { measurables, constraints ->
        val child = measurables.first().measure(constraints.copy(minWidth = 0, minHeight = 0))
        val width = if (effects.growWidth) Math.round(child.width * shown) else child.width
        val height = if (effects.growHeight) Math.round(child.height * shown) else child.height
        layout(width, height) {
            child.place(Math.round((effects.slideX?.invoke(child.width) ?: 0) * away), Math.round((effects.slideY?.invoke(child.height) ?: 0) * away))
        }
    }
}

/** [content] for [targetState]: when that changes, what it was goes at once and what it is now fades in. */
@Composable
fun <T> Crossfade(targetState: T, modifier: Modifier = Modifier, animationSpec: FiniteAnimationSpec<Float> = tween(),
                  @Suppress("UNUSED_PARAMETER") label: String = "Crossfade", content: @Composable (T) -> Unit) {
    key(targetState) {
        var shown by remember { mutableStateOf(false) }
        LaunchedEffect(Unit) { shown = true }
        val alpha by animateFloatAsState(if (shown) 1f else 0f, animationSpec)
        Box(modifier.alpha(alpha)) { content(targetState) }
    }
}
