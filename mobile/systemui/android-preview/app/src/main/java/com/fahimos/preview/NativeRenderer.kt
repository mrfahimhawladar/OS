package com.fahimos.preview

object NativeRenderer {
    init {
        System.loadLibrary("fahim_lockscreen_preview")
    }

    external fun renderFrame(
        mode: Int,
        pinDigits: Int,
        message: String?,
        time: String,
        date: String,
        network: String,
        battery: String
    ): IntArray

    external fun hitTestPin(x: Int, y: Int): Int
}
