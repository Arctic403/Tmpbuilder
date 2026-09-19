package com.codynex.n0lab

import android.app.Activity
import android.os.Bundle
import android.widget.ScrollView
import android.widget.TextView

class MainActivity : Activity() {
    companion object {
        init {
            System.loadLibrary("codynex_n0")
        }
    }

    private external fun nativeRunAll(): String

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val output = TextView(this).apply {
            text = "Codynex N0 native lab running…"
            setTextIsSelectable(true)
            setPadding(24, 24, 24, 24)
        }

        val scroll = ScrollView(this).apply {
            addView(output)
        }

        setContentView(scroll)

        Thread {
            val result = try {
                nativeRunAll()
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
