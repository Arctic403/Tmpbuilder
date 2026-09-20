package com.codynex.lr0lab

import android.app.Activity
import android.os.Bundle
import android.os.Process
import android.text.InputType
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import java.io.File

class MainActivity : Activity() {
    companion object {
        init {
            System.loadLibrary("codynex_lr0")
        }
    }

    private external fun nativeOpen(
        rootDirectory: String
    ): String

    private external fun nativeActivateCandidate(
        candidatePath: String
    ): String

    private external fun nativeCall(
        functionId: Int
    ): String

    private external fun nativeSnapshot(): String

    private external fun nativeReadState(
        stateId: Int
    ): String

    private lateinit var runtimeRoot: File
    private lateinit var candidateFile: File
    private lateinit var editor: EditText
    private lateinit var output: TextView
    private lateinit var functionId: EditText
    private lateinit var stateId: EditText

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        runtimeRoot =
            File(filesDir, "lr0-runtime").apply {
                mkdirs()
            }

        candidateFile =
            File(
                File(filesDir, "lr0-candidates").apply {
                    mkdirs()
                },
                "candidate.cxe"
            )

        editor = EditText(this).apply {
            setText(CxeAssembler.PROGRAM_A)
            setTextIsSelectable(true)
            isSingleLine = false
            minLines = 18
            inputType =
                InputType.TYPE_CLASS_TEXT or
                InputType.TYPE_TEXT_FLAG_MULTI_LINE or
                InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            setPadding(24, 20, 24, 20)
        }

        output = TextView(this).apply {
            text = "Opening LR0 runtime…"
            setTextIsSelectable(true)
            setPadding(24, 24, 24, 24)
        }

        functionId = numericField("0")
        stateId = numericField("0")

        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(12, 12, 12, 24)

            addView(TextView(this@MainActivity).apply {
                text =
                    "Codynex LR0 Lab — editor/test equipment. " +
                    "The native runtime only accepts external CXE1 files."
                setPadding(24, 12, 24, 12)
            })

            addView(button("Load Program A template") {
                editor.setText(CxeAssembler.PROGRAM_A)
            })

            addView(button("Load Program B template") {
                editor.setText(CxeAssembler.PROGRAM_B)
            })

            addView(editor)

            addView(button("Compile editor → external candidate.cxe") {
                val sourceText = editor.text.toString()

                runAsync {
                    compileCandidate(sourceText)
                }
            })

            addView(button("Activate existing candidate.cxe") {
                runAsync {
                    nativeActivateCandidate(
                        candidateFile.absolutePath
                    )
                }
            })

            addView(button("Compile + activate") {
                val sourceText = editor.text.toString()

                runAsync {
                    val compile =
                        compileCandidate(sourceText)

                    if (!candidateFile.isFile) {
                        compile
                    } else {
                        compile +
                            "\n\n" +
                            nativeActivateCandidate(
                                candidateFile.absolutePath
                            )
                    }
                }
            })

            addView(button("Corrupt candidate integrity byte") {
                runAsync {
                    corruptCandidate()
                }
            })

            addView(TextView(this@MainActivity).apply {
                text = "Function id:"
                setPadding(24, 16, 24, 4)
            })

            addView(functionId)

            addView(button("Call function") {
                val id = functionId.text
                    .toString()
                    .toIntOrNull()

                if (id == null) {
                    output.text = "Invalid function id."
                } else {
                    runAsync {
                        nativeCall(id)
                    }
                }
            })

            addView(TextView(this@MainActivity).apply {
                text = "State id:"
                setPadding(24, 16, 24, 4)
            })

            addView(stateId)

            addView(button("Read state") {
                val id = stateId.text
                    .toString()
                    .toIntOrNull()

                if (id == null) {
                    output.text = "Invalid state id."
                } else {
                    runAsync {
                        nativeReadState(id)
                    }
                }
            })

            addView(button("Runtime snapshot") {
                runAsync {
                    nativeSnapshot()
                }
            })

            addView(button("Re-open / recover now") {
                runAsync {
                    nativeOpen(runtimeRoot.absolutePath)
                }
            })

            addView(button("CLEAR runtime recovery store") {
                runAsync {
                    runtimeRoot.deleteRecursively()
                    runtimeRoot.mkdirs()
                    nativeOpen(runtimeRoot.absolutePath)
                }
            })

            addView(button("KILL PROCESS for cold-restart proof") {
                output.text =
                    "Process will terminate now. Reopen the app; " +
                    "nativeOpen() must reconstruct from the recovery store."

                output.postDelayed(
                    {
                        Process.killProcess(Process.myPid())
                    },
                    250L
                )
            })

            addView(TextView(this@MainActivity).apply {
                text =
                    "Candidate file:\n" +
                    candidateFile.absolutePath +
                    "\n\nRecovery store:\n" +
                    runtimeRoot.absolutePath
                setTextIsSelectable(true)
                setPadding(24, 20, 24, 8)
            })

            addView(output)
        }

        setContentView(
            ScrollView(this).apply {
                addView(
                    layout,
                    ViewGroup.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT
                    )
                )
            }
        )

        runAsync {
            nativeOpen(runtimeRoot.absolutePath)
        }
    }

    private fun compileCandidate(
        sourceText: String
    ): String {
        val assembled =
            CxeAssembler.assemble(sourceText)

        if (!assembled.ok) {
            if (candidateFile.exists()) {
                candidateFile.delete()
            }

            return "ASSEMBLY FAILED\n${assembled.message}"
        }

        candidateFile.writeBytes(assembled.bytes)

        return buildString {
            append("ASSEMBLY OK\n")
            append(assembled.message)
            append("\nexternal file: ")
            append(candidateFile.absolutePath)
            append("\nfile bytes: ")
            append(candidateFile.length())
        }
    }

    private fun corruptCandidate(): String {
        if (!candidateFile.isFile) {
            return "No candidate file exists."
        }

        val bytes = candidateFile.readBytes()

        if (bytes.isEmpty()) {
            return "Candidate file is empty."
        }

        bytes[bytes.lastIndex] =
            (bytes.last().toInt() xor 0x01).toByte()

        candidateFile.writeBytes(bytes)

        return "Candidate integrity trailer corrupted. " +
            "Activation should fail while the active program/state survives."
    }

    private fun numericField(defaultValue: String): EditText =
        EditText(this).apply {
            setText(defaultValue)
            inputType = InputType.TYPE_CLASS_NUMBER
            isSingleLine = true
            setPadding(24, 4, 24, 8)
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
        action: () -> String
    ) {
        output.text = "Running…"

        Thread {
            val result = try {
                action()
            } catch (error: Throwable) {
                "ERROR\n${error}"
            }

            runOnUiThread {
                output.text = result
            }
        }.start()
    }
}
