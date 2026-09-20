package com.codynex.lr0lab

import java.io.ByteArrayOutputStream

object CxeAssembler {
    data class Result(
        val ok: Boolean,
        val bytes: ByteArray = ByteArray(0),
        val message: String
    )

    private data class State(
        val id: Int,
        val persistent: Boolean,
        val initial: Long
    )

    private data class Function(
        val id: Int,
        val code: ByteArray
    )

    const val PROGRAM_A = """# CXE1 lab assembly
state 0 persistent 0

func 0
load 0
const 1
add
store 0
load 0
return
end

func 1
load 0
return
end
"""

    const val PROGRAM_B = """# Same state schema, changed behavior
state 0 persistent 0

func 0
load 0
const 5
add
store 0
load 0
return
end

func 1
load 0
return
end
"""

    fun assemble(source: String): Result {
        val states = mutableListOf<State>()
        val functions = mutableListOf<Function>()
        var currentFunctionId: Int? = null
        var currentCode: ByteArrayOutputStream? = null

        val lines = source.lineSequence().toList()

        for ((index, raw) in lines.withIndex()) {
            val lineNumber = index + 1
            val line = raw.substringBefore("#").trim()

            if (line.isEmpty()) {
                continue
            }

            val parts = line.split(Regex("\\s+"))
            val op = parts.first().lowercase()

            try {
                when (op) {
                    "state" -> {
                        if (currentFunctionId != null) {
                            return fail(lineNumber, "state inside function")
                        }
                        if (parts.size != 4) {
                            return fail(
                                lineNumber,
                                "state syntax: state <id> <persistent|volatile> <initial-i64>"
                            )
                        }

                        val id = parseU16(parts[1])
                        if (id != states.size) {
                            return fail(
                                lineNumber,
                                "state ids must be dense from 0"
                            )
                        }

                        val persistence = parts[2].lowercase()
                        val persistent = when (persistence) {
                            "persistent" -> true
                            "volatile" -> false
                            else -> return fail(
                                lineNumber,
                                "state persistence must be persistent or volatile"
                            )
                        }

                        val initial = parts[3].toLong()
                        states += State(id, persistent, initial)
                    }

                    "func" -> {
                        if (currentFunctionId != null) {
                            return fail(
                                lineNumber,
                                "nested func is not allowed"
                            )
                        }
                        if (parts.size != 2) {
                            return fail(
                                lineNumber,
                                "func syntax: func <id>"
                            )
                        }

                        val id = parseU16(parts[1])
                        if (id != functions.size) {
                            return fail(
                                lineNumber,
                                "function ids must be dense from 0"
                            )
                        }

                        currentFunctionId = id
                        currentCode = ByteArrayOutputStream()
                    }

                    "end" -> {
                        val id = currentFunctionId
                            ?: return fail(
                                lineNumber,
                                "end without func"
                            )
                        if (parts.size != 1) {
                            return fail(
                                lineNumber,
                                "end takes no operands"
                            )
                        }

                        val code = currentCode?.toByteArray()
                            ?: return fail(
                                lineNumber,
                                "function code missing"
                            )

                        if (code.isEmpty()) {
                            return fail(
                                lineNumber,
                                "function cannot be empty"
                            )
                        }

                        functions += Function(id, code)
                        currentFunctionId = null
                        currentCode = null
                    }

                    "const" -> {
                        val code = requireFunction(
                            currentFunctionId,
                            currentCode,
                            lineNumber
                        ) ?: return fail(
                            lineNumber,
                            "const outside function"
                        )
                        if (parts.size != 2) {
                            return fail(
                                lineNumber,
                                "const syntax: const <i64>"
                            )
                        }

                        code.write(0x01)
                        writeI64(code, parts[1].toLong())
                    }

                    "load" -> {
                        val code = requireFunction(
                            currentFunctionId,
                            currentCode,
                            lineNumber
                        ) ?: return fail(
                            lineNumber,
                            "load outside function"
                        )
                        if (parts.size != 2) {
                            return fail(
                                lineNumber,
                                "load syntax: load <state-id>"
                            )
                        }

                        code.write(0x02)
                        writeU16(code, parseU16(parts[1]))
                    }

                    "store" -> {
                        val code = requireFunction(
                            currentFunctionId,
                            currentCode,
                            lineNumber
                        ) ?: return fail(
                            lineNumber,
                            "store outside function"
                        )
                        if (parts.size != 2) {
                            return fail(
                                lineNumber,
                                "store syntax: store <state-id>"
                            )
                        }

                        code.write(0x03)
                        writeU16(code, parseU16(parts[1]))
                    }

                    "add" -> {
                        val code = requireFunction(
                            currentFunctionId,
                            currentCode,
                            lineNumber
                        ) ?: return fail(
                            lineNumber,
                            "add outside function"
                        )
                        if (parts.size != 1) {
                            return fail(
                                lineNumber,
                                "add takes no operands"
                            )
                        }

                        code.write(0x04)
                    }

                    "return" -> {
                        val code = requireFunction(
                            currentFunctionId,
                            currentCode,
                            lineNumber
                        ) ?: return fail(
                            lineNumber,
                            "return outside function"
                        )
                        if (parts.size != 1) {
                            return fail(
                                lineNumber,
                                "return takes no operands"
                            )
                        }

                        code.write(0x05)
                    }

                    else -> return fail(
                        lineNumber,
                        "unknown assembly instruction: $op"
                    )
                }
            } catch (error: NumberFormatException) {
                return fail(
                    lineNumber,
                    "invalid numeric value: ${error.message ?: "number"}"
                )
            } catch (error: IllegalArgumentException) {
                return fail(
                    lineNumber,
                    error.message ?: "invalid operand"
                )
            }
        }

        if (currentFunctionId != null) {
            return Result(
                false,
                message = "unterminated func $currentFunctionId"
            )
        }

        if (states.size > 64) {
            return Result(false, message = "state limit exceeded")
        }

        if (functions.size > 64) {
            return Result(false, message = "function limit exceeded")
        }

        return buildExecutable(states, functions)
    }

    private fun buildExecutable(
        states: List<State>,
        functions: List<Function>
    ): Result {
        val codeBytes = functions.sumOf { it.code.size }

        if (codeBytes > 48 * 1024) {
            return Result(false, message = "code limit exceeded")
        }

        val totalBytes =
            28 +
            states.size * 12 +
            functions.size * 12 +
            codeBytes +
            8

        if (totalBytes > 64 * 1024) {
            return Result(false, message = "executable limit exceeded")
        }

        val out = ByteArrayOutputStream(totalBytes)

        out.write(byteArrayOf('C'.code.toByte(), 'X'.code.toByte(), 'E'.code.toByte(), '1'.code.toByte()))
        writeU16(out, 1)
        writeU16(out, 0)
        writeU16(out, states.size)
        writeU16(out, functions.size)
        writeU32(out, codeBytes.toLong())
        writeU32(out, totalBytes.toLong())
        writeU64(out, schemaFingerprint(states))

        for (state in states) {
            writeU16(out, state.id)
            out.write(1)
            out.write(if (state.persistent) 1 else 0)
            writeI64(out, state.initial)
        }

        var codeOffset = 0

        for (function in functions) {
            writeU16(out, function.id)
            writeU16(out, 0)
            writeU32(out, codeOffset.toLong())
            writeU32(out, function.code.size.toLong())
            codeOffset += function.code.size
        }

        for (function in functions) {
            out.write(function.code)
        }

        val withoutIntegrity = out.toByteArray()
        writeU64(out, fnv1a64(withoutIntegrity))

        val bytes = out.toByteArray()

        if (bytes.size != totalBytes) {
            return Result(
                false,
                message = "internal size mismatch"
            )
        }

        return Result(
            true,
            bytes,
            "assembled ${bytes.size} bytes"
        )
    }

    private fun requireFunction(
        functionId: Int?,
        code: ByteArrayOutputStream?,
        lineNumber: Int
    ): ByteArrayOutputStream? {
        if (functionId == null || code == null) {
            return null
        }

        if (code.size() > 48 * 1024) {
            throw IllegalArgumentException(
                "line $lineNumber: code limit exceeded"
            )
        }

        return code
    }

    private fun parseU16(value: String): Int {
        val parsed = value.toInt()

        require(parsed in 0..65535) {
            "u16 operand out of range"
        }

        return parsed
    }

    private fun schemaFingerprint(
        states: List<State>
    ): ULong {
        var hash = FNV_OFFSET

        for (state in states) {
            if (!state.persistent) {
                continue
            }

            hash = fnvByte(hash, state.id and 0xff)
            hash = fnvByte(hash, (state.id ushr 8) and 0xff)
            hash = fnvByte(hash, 1)
            hash = fnvByte(hash, 1)
        }

        return hash
    }

    private fun fnv1a64(bytes: ByteArray): ULong {
        var hash = FNV_OFFSET

        for (byte in bytes) {
            hash = fnvByte(hash, byte.toInt() and 0xff)
        }

        return hash
    }

    private fun fnvByte(
        hash: ULong,
        value: Int
    ): ULong =
        (hash xor value.toULong()) * FNV_PRIME

    private fun writeU16(
        out: ByteArrayOutputStream,
        value: Int
    ) {
        out.write(value and 0xff)
        out.write((value ushr 8) and 0xff)
    }

    private fun writeU32(
        out: ByteArrayOutputStream,
        value: Long
    ) {
        require(value in 0..0xffffffffL) {
            "u32 value out of range"
        }

        for (shift in 0 until 32 step 8) {
            out.write(
                ((value ushr shift) and 0xffL).toInt()
            )
        }
    }

    private fun writeU64(
        out: ByteArrayOutputStream,
        value: ULong
    ) {
        for (shift in 0 until 64 step 8) {
            out.write(
                ((value shr shift) and 0xffuL).toInt()
            )
        }
    }

    private fun writeI64(
        out: ByteArrayOutputStream,
        value: Long
    ) {
        writeU64(out, value.toULong())
    }

    private fun fail(
        lineNumber: Int,
        message: String
    ): Result =
        Result(
            false,
            message = "line $lineNumber: $message"
        )

    private val FNV_OFFSET = 1469598103934665603uL
    private val FNV_PRIME = 1099511628211uL
}
