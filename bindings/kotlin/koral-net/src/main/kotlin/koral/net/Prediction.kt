package koral.net

/** knet::TickClock: a fixed-rate simulation clock. */
class TickClock(ticksPerSecond: Double = 60.0, private val maxTicksPerAdvance: Int = 8) {
    val interval: Double = 1.0 / ticksPerSecond
    private var accumulator = 0.0
    var tick: Int = 0
        private set
    /** How far into the next tick, 0..1. */
    val alpha: Float get() = (accumulator / interval).toFloat()

    /** Adds real time; returns how many ticks are due (capped: a long stall is dropped, not replayed). */
    fun advance(seconds: Double): Int {
        accumulator += maxOf(0.0, seconds)
        var ticks = (accumulator / interval).toInt()
        if (ticks > maxTicksPerAdvance) {
            ticks = maxTicksPerAdvance
            accumulator = 0.0
        } else {
            accumulator -= ticks * interval
        }
        tick += ticks
        return ticks
    }
    fun reset(tick: Int) { this.tick = tick; accumulator = 0.0 }
}

/** knet::Predictor: the local state run ahead on local input, corrected by the server's. */
class Predictor<S, I>(initial: S, private val step: (S, I) -> S, private val maxPending: Int = 256) {
    private val pendingInputs = ArrayDeque<Pair<Int, I>>()
    var current: S = initial
        private set
    var tick: Int = 0
        private set
    var confirmed: Int = 0
        private set
    var reconciliations: Long = 0
        private set
    /** Inputs the server has not confirmed: send these each tick. */
    val pending: List<Pair<Int, I>> get() = pendingInputs

    /** Runs [input] for the next tick, at once; returns that tick. */
    fun apply(input: I): Int {
        val t = ++tick
        pendingInputs.addLast(t to input)
        if (pendingInputs.size > maxPending) pendingInputs.removeFirst()
        current = step(current, input)
        return t
    }

    /** The server's state after its tick [tick]: rewind to it, replay what came after. */
    fun reconcile(tick: Int, authoritative: S) {
        if (tick < confirmed) return
        confirmed = tick
        while (pendingInputs.isNotEmpty() && pendingInputs.first().first <= tick) pendingInputs.removeFirst()
        var state = authoritative
        for ((_, input) in pendingInputs) state = step(state, input)
        current = state
        ++reconciliations
    }
}

/** knet::InputBuffer: a client's inputs by tick, handed out in order; a missing one repeats the last. */
class InputBuffer<I>(private val capacity: Int = 256) {
    private val inputs = java.util.TreeMap<Int, I>()
    private var taken = false
    private var lastTaken = 0
    private var last: I? = null
    var missed: Long = 0
        private set
    var received: Long = 0
        private set
    val size: Int get() = inputs.size
    val newestTick: Int? get() = if (inputs.isEmpty()) null else inputs.lastKey()

    fun add(tick: Int, input: I) {
        if (taken && tick <= lastTaken) return
        inputs.putIfAbsent(tick, input)
        while (inputs.size > capacity) inputs.pollFirstEntry()
    }

    /** The input for [tick], or the last one taken; null before any. */
    fun take(tick: Int): I? {
        inputs.headMap(tick, false).clear()
        taken = true
        lastTaken = tick
        val found = inputs.remove(tick)
        if (found != null) { last = found; ++received } else ++missed
        return last
    }
}
