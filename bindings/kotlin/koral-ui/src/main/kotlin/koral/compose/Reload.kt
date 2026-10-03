package koral.compose

import java.lang.classfile.ClassFile
import java.lang.classfile.CodeModel
import java.lang.classfile.Instruction
import java.lang.classfile.Label
import java.lang.classfile.Opcode
import java.lang.classfile.instruction.BranchInstruction
import java.lang.classfile.instruction.ConstantInstruction
import java.lang.classfile.instruction.ExceptionCatch
import java.lang.classfile.instruction.InvokeDynamicInstruction
import java.lang.classfile.instruction.InvokeInstruction
import java.lang.classfile.instruction.LabelTarget
import java.lang.classfile.instruction.LoadInstruction
import java.lang.classfile.instruction.LookupSwitchInstruction
import java.lang.classfile.instruction.TableSwitchInstruction
import java.lang.constant.DirectMethodHandleDesc
import java.util.Collections
import java.util.WeakHashMap
import koral.HotReload
import koral.Log

/**
 * What a hot reload does to Compose interfaces.
 *
 * Every composition composes again with the new code, keeping what it remembers. A composable whose own
 * code changed can no longer read its remembered values by position: an added call shifts them. So its
 * groups are discarded first — Compose's `invalidateGroupsWithKey`, as Android's Live Edit uses — and it
 * starts afresh, while everything around it keeps its state.
 *
 * Which composables changed is read from the class files: methods whose code differs, and the group key
 * each one composes under — the constant given to `startRestartGroup` in a composable function, or to
 * `rememberComposableLambda` where a composable lambda is made.
 */
internal object ComposeReload {
    private val uis: MutableSet<ComposeUi> = Collections.synchronizedSet(Collections.newSetFromMap(WeakHashMap()))
    private val listener: (HotReload.Reload) -> Unit = ::reloaded
    @Volatile private var installed = false

    fun track(ui: ComposeUi) {
        if (!installed) synchronized(this) {
            if (!installed) { HotReload.onReloaded(listener); installed = true }
        }
        uis += ui
    }

    fun untrack(ui: ComposeUi) { uis -= ui }

    private fun reloaded(reload: HotReload.Reload) {
        val keys = reload.after.flatMap { (name, after) ->
            try { changedGroupKeys(reload.before[name], after) } catch (e: Exception) {
                Log.warn("[koral.compose] could not compare the old and new $name: $e"); emptySet()
            }
        }.toSet()
        for (key in keys) androidx.compose.runtime.invalidateGroupsWithKey(key)
        val live = synchronized(uis) { uis.toList() }
        for (ui in live) ui.reassemble()
    }

    /** The group keys of the composables whose code differs between [before] and [after], in either version. */
    fun changedGroupKeys(before: ByteArray?, after: ByteArray): Set<Int> {
        val old = before?.let { methods(it) } ?: emptyMap()
        val new = methods(after)
        val changed = new.keys.filter { old[it]?.text != new.getValue(it).text } + old.keys.filter { it !in new }
        if (changed.isEmpty()) return emptySet()
        val keys = mutableSetOf<Int>()
        for (version in listOf(old, new)) {
            val lambdaKeys = version.values.flatMap { it.lambdaKeys.entries }.associate { it.key to it.value }
            for (method in changed) {
                val m = version[method] ?: continue
                m.restartKey?.let(keys::add)
                lambdaKeys[method.substringBefore('(')]?.let(keys::add)
            }
        }
        return keys
    }

    private class Method(val text: String, val restartKey: Int?, val lambdaKeys: Map<String, Int>)

    /** Each method, by name and descriptor: its code, written so that only a real change shows. */
    private fun methods(bytes: ByteArray): Map<String, Method> =
        ClassFile.of().parse(bytes).methods().associate { m ->
            val id = m.methodName().stringValue() + m.methodType().stringValue()
            val code = m.code().orElse(null)
            id to if (code == null) Method("", null, emptyMap()) else read(code)
        }

    private fun read(code: CodeModel): Method {
        val labels = HashMap<Label, Int>()
        fun label(l: Label) = "L" + labels.getOrPut(l) { labels.size }
        val text = mutableListOf<String>()
        val instructions = mutableListOf<Instruction>()
        var restartKey: Int? = null
        val lambdaKeys = HashMap<String, Int>()
        val strings = ArrayDeque<Int>()   // where the last string constants were written
        for (element in code.elementList()) {
            text += when (element) {
                is LabelTarget -> label(element.label()) + ":"
                is BranchInstruction -> "${element.opcode()} ${label(element.target())}"
                is TableSwitchInstruction -> "${element.opcode()} ${label(element.defaultTarget())} " +
                    element.cases().joinToString { "${it.caseValue()}:${label(it.target())}" }
                is LookupSwitchInstruction -> "${element.opcode()} ${label(element.defaultTarget())} " +
                    element.cases().joinToString { "${it.caseValue()}:${label(it.target())}" }
                is ExceptionCatch -> "catch ${label(element.tryStart())} ${label(element.tryEnd())} ${label(element.handler())} " +
                    element.catchType().map { it.asInternalName() }.orElse("*")
                is InvokeDynamicInstruction -> "indy ${element.name().stringValue()} " +
                    element.bootstrapArgs().joinToString { (it as? DirectMethodHandleDesc)?.methodName() ?: it.toString() }
                is Instruction -> element.toString()
                else -> continue   // line numbers, local variable names: what moving code around changes
            }
            if (element !is Instruction) continue
            instructions += element
            if (element is ConstantInstruction && (element.constantValue() as Any) is String) {
                strings.addLast(text.size - 1)
                if (strings.size > 4) strings.removeFirst()
            }

            if (element is InvokeInstruction) {
                val name = element.name().stringValue()
                // Compose's tooling information gives source offsets and line numbers, which any edit above moves.
                if (name in TOOLING && element.owner().asInternalName() == "androidx/compose/runtime/ComposerKt") {
                    for (at in strings) text[at] = "(source information)"
                    strings.clear()
                }
                if (restartKey == null && name == "startRestartGroup" && element.owner().asInternalName() == "androidx/compose/runtime/Composer")
                    restartKey = intBefore(instructions, instructions.size - 1)
                if (name == "rememberComposableLambda" || name == "composableLambda" || name == "composableLambdaInstance")
                    lambdaKeyOf(instructions)?.let { (method, key) -> lambdaKeys[method] = key }
            }
        }
        return Method(text.joinToString("\n"), restartKey, lambdaKeys)
    }

    private val TOOLING = setOf("sourceInformation", "sourceInformationMarkerStart", "traceEventStart")

    /** The int constant pushed right before instruction [at]. */
    private fun intBefore(instructions: List<Instruction>, at: Int): Int? =
        (instructions.getOrNull(at - 1) as? ConstantInstruction)?.constantValue() as? Int

    /**
     * At a call making a composable lambda — `rememberComposableLambda(key, tracked, block, ...)` — the method
     * the block runs and the key: the block is the last lambda made before it, and the key the int constant
     * before the `tracked` boolean before that.
     */
    private fun lambdaKeyOf(instructions: List<Instruction>): Pair<String, Int>? {
        var i = instructions.size - 1
        while (i >= 0 && instructions[i] !is InvokeDynamicInstruction) i--
        if (i < 0) return null
        val block = (instructions[i] as InvokeDynamicInstruction).bootstrapArgs()
            .firstNotNullOfOrNull { it as? DirectMethodHandleDesc }?.methodName() ?: return null
        var j = i - 1
        while (j >= 0 && instructions[j] is LoadInstruction) j--   // what the lambda captures
        val tracked = instructions.getOrNull(j)?.opcode()
        if (tracked != Opcode.ICONST_0 && tracked != Opcode.ICONST_1) return null
        val key = intBefore(instructions, j) ?: return null
        return block to key
    }
}
