package com.codynex.n2lab

import android.app.Activity
import android.os.Bundle
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import java.io.File

class MainActivity : Activity() {
    companion object {
        init {
            System.loadLibrary("codynex_n2")
        }

        private const val GRAPH = 1
        private const val TAPE = 2
    }

    private external fun nativeRunAll(): String

    private external fun nativePrepareCold(
        path: String,
        preRepresentation: Int,
        damageCase: Boolean
    ): String

    private external fun nativeResumeCold(
        path: String,
        postRepresentation: Int
    ): String

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val checkpointPath =
            File(filesDir, "n2-cold-proof.bin").absolutePath

        val output = TextView(this).apply {
            text = "Codynex N2 native lab ready."
            setTextIsSelectable(true)
            setPadding(24, 24, 24, 24)
        }

        val instructions = TextView(this).apply {
            text =
                "Cold proof rule: after PREPARE succeeds, fully terminate " +
                "this app process before reopening it and pressing the " +
                "opposite RESUME button. The checkpoint stores substrate " +
                "evidence only; it does not store the prior representation."
            setPadding(24, 12, 24, 24)
        }

        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(instructions)
            addView(button("Run N2 in-process suite") {
                runAsync(output) { nativeRunAll() }
            })
            addView(button("PREPARE Graph → Tape (version)") {
                runAsync(output) {
                    nativePrepareCold(
                        checkpointPath,
                        GRAPH,
                        false
                    )
                }
            })
            addView(button("PREPARE Tape → Graph (repair)") {
                runAsync(output) {
                    nativePrepareCold(
                        checkpointPath,
                        TAPE,
                        true
                    )
                }
            })
            addView(button("RESUME checkpoint as Graph") {
                runAsync(output) {
                    nativeResumeCold(
                        checkpointPath,
                        GRAPH
                    )
                }
            })
            addView(button("RESUME checkpoint as Tape") {
                runAsync(output) {
                    nativeResumeCold(
                        checkpointPath,
                        TAPE
                    )
                }
            })
            addView(output)
        }

        setContentView(
            ScrollView(this).apply {
                addView(layout)
            }
        )
    }

    private fun button(
        label: String,
        action: () -> Unit
    ): Button =
        Button(this).apply {
            text = label
            setOnClickListener { action() }
        }

    private fun runAsync(
        output: TextView,
        action: () -> String
    ) {
        output.text = "Running…"

        Thread {
            val result = try {
                action()
            } catch (error: Throwable) {
                """{"pass":false,"error":"${escape(error.toString())}"}"""
            }

            runOnUiThread {
                output.text = result
            }
        }.start()
    }

    private fun escape(value: String): String =
        value
            .replace("\\", "\\\\")
            .replace("\"", "\\\"")
            .replace("\n", "\\n")
}
