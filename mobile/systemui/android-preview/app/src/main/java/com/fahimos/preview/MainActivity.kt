package com.fahimos.preview

import android.app.Activity
import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.os.BatteryManager
import android.os.Bundle
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.HorizontalScrollView
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.min

class MainActivity : Activity() {
    companion object {
        private const val FRAME_WIDTH = 440f
        private const val FRAME_HEIGHT = 900f
        private const val MODE_LOCK = 0
        private const val MODE_PIN = 1
        private const val MODE_WRONG_PIN = 2
        private const val MODE_LOCKOUT = 3
        private const val MODE_FLASHLIGHT = 4
    }

    private lateinit var previewImage: ImageView
    private lateinit var modeLabel: TextView
    private var mode = MODE_LOCK
    private var pinDigits = 0
    private var downX = 0f
    private var downY = 0f
    private var lastTouchTime = 0L

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        window.decorView.systemUiVisibility =
            View.SYSTEM_UI_FLAG_FULLSCREEN or
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE or
            View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or
            View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
        buildPreviewUi()
        render()
    }

    private fun dp(value: Int): Int =
        (value * resources.displayMetrics.density).toInt()

    private fun buildPreviewUi() {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Color.rgb(8, 14, 24))
            setPadding(dp(12), dp(8), dp(12), dp(6))
        }

        val heading = TextView(this).apply {
            text = "FAHIM OS  /  NATIVE LOCK SCREEN"
            setTextColor(Color.rgb(220, 232, 246))
            textSize = 13f
            letterSpacing = 0.08f
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(4), dp(4), dp(4), dp(8))
        }
        root.addView(heading, LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, dp(34)
        ))

        modeLabel = TextView(this).apply {
            text = "LIVE NATIVE RENDERER"
            setTextColor(Color.rgb(150, 177, 207))
            textSize = 10f
            letterSpacing = 0.12f
            setPadding(dp(4), 0, dp(4), dp(7))
        }
        root.addView(modeLabel)

        val tabs = HorizontalScrollView(this).apply {
            isHorizontalScrollBarEnabled = false
            setBackgroundColor(Color.rgb(14, 24, 38))
        }
        val tabRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(4), dp(5), dp(4), dp(5))
        }
        tabs.addView(tabRow)
        addModeButton(tabRow, "Lock", MODE_LOCK)
        addModeButton(tabRow, "PIN", MODE_PIN)
        addModeButton(tabRow, "Wrong PIN", MODE_WRONG_PIN)
        addModeButton(tabRow, "Lockout", MODE_LOCKOUT)
        addModeButton(tabRow, "Flashlight", MODE_FLASHLIGHT)
        root.addView(tabs, LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, dp(46)
        ))

        previewImage = ImageView(this).apply {
            scaleType = ImageView.ScaleType.FIT_CENTER
            setBackgroundColor(Color.rgb(8, 14, 24))
            contentDescription = "Fahim OS native lock screen render. Swipe up to open the PIN state."
        }
        previewImage.setOnTouchListener { _, event -> handlePreviewTouch(event) }
        root.addView(previewImage, LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f
        ))

        val footer = TextView(this).apply {
            text = "RENDERED BY NATIVE C++  •  PREVIEW ONLY"
            setTextColor(Color.rgb(111, 133, 157))
            textSize = 9f
            letterSpacing = 0.10f
            gravity = Gravity.CENTER
            setPadding(0, dp(5), 0, dp(2))
        }
        root.addView(footer, LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, dp(24)
        ))

        setContentView(root)
    }

    private fun addModeButton(parent: LinearLayout, title: String, targetMode: Int) {
        val button = Button(this).apply {
            text = title
            isAllCaps = false
            textSize = 11f
            setTextColor(Color.rgb(224, 235, 248))
            background = GradientDrawable().apply {
                cornerRadius = dp(18).toFloat()
                setColor(Color.rgb(29, 45, 65))
                setStroke(dp(1), Color.rgb(55, 78, 105))
            }
            setPadding(dp(9), 0, dp(9), 0)
            minHeight = dp(34)
            minimumHeight = dp(34)
            setOnClickListener {
                mode = targetMode
                if (mode == MODE_LOCK || mode == MODE_FLASHLIGHT) pinDigits = 0
                if (mode == MODE_PIN) pinDigits = 0
                render()
            }
        }
        parent.addView(button, LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.WRAP_CONTENT, dp(34)
        ).apply {
            marginStart = dp(3)
            marginEnd = dp(3)
        })
    }

    private fun handlePreviewTouch(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                downX = event.x
                downY = event.y
                lastTouchTime = event.eventTime
                return true
            }
            MotionEvent.ACTION_UP -> {
                val dx = event.x - downX
                val dy = event.y - downY
                if (mode == MODE_LOCK && dy < -dp(35)) {
                    mode = MODE_PIN
                    pinDigits = 0
                    render()
                    return true
                }
                if (mode == MODE_FLASHLIGHT && kotlin.math.abs(dy) < dp(20)) {
                    mode = MODE_LOCK
                    render()
                    return true
                }
                if (mode == MODE_PIN || mode == MODE_WRONG_PIN || mode == MODE_LOCKOUT) {
                    handlePinTap(event.x, event.y)
                }
                return true
            }
        }
        return true
    }

    private fun handlePinTap(viewX: Float, viewY: Float) {
        val imageWidth = previewImage.width.toFloat()
        val imageHeight = previewImage.height.toFloat()
        val scale = min(imageWidth / FRAME_WIDTH, imageHeight / FRAME_HEIGHT)
        if (scale <= 0f) return
        val drawnWidth = FRAME_WIDTH * scale
        val drawnHeight = FRAME_HEIGHT * scale
        val left = (imageWidth - drawnWidth) / 2f
        val top = (imageHeight - drawnHeight) / 2f
        val x = ((viewX - left) / scale).toInt()
        val y = ((viewY - top) / scale).toInt()
        if (x !in 0..439 || y !in 0..899) return

        when (NativeRenderer.hitTestPin(x, y)) {
            in 1..10 -> {
                if (mode == MODE_LOCKOUT) return
                pinDigits = (pinDigits + 1).coerceAtMost(6)
                mode = MODE_PIN
                render()
            }
            11 -> {
                pinDigits = (pinDigits - 1).coerceAtLeast(0)
                mode = MODE_PIN
                render()
            }
            12 -> {
                mode = MODE_WRONG_PIN
                render()
            }
            13 -> {
                mode = MODE_LOCK
                pinDigits = 0
                render()
            }
        }
    }

    private fun render() {
        if (!::previewImage.isInitialized) return

        val now = Date()
        val time = SimpleDateFormat("h:mm", Locale.US).format(now)
        val date = SimpleDateFormat("EEE, MMM d", Locale.US).format(now)
        val batteryManager = getSystemService(BATTERY_SERVICE) as BatteryManager
        val capacity = batteryManager.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY)
        val battery = if (capacity in 0..100) "$capacity%" else "86%"
        val network = "MOBILE"

        val message = when (mode) {
            MODE_WRONG_PIN -> "INCORRECT PIN"
            MODE_LOCKOUT -> "TRY AGAIN LATER"
            else -> null
        }

        val pixels = NativeRenderer.renderFrame(
            mode, pinDigits, message, time, date, network, battery
        )
        val bitmap = Bitmap.createBitmap(
            pixels, 440, 900, Bitmap.Config.ARGB_8888
        )
        previewImage.setImageBitmap(bitmap)
        modeLabel.text = when (mode) {
            MODE_LOCK -> "LOCK SCREEN  ·  SWIPE UP FOR PIN"
            MODE_PIN -> "PIN BOUNCER  ·  TAP THE NATIVE KEYPAD"
            MODE_WRONG_PIN -> "INCORRECT PIN STATE  ·  VISUAL PREVIEW"
            MODE_LOCKOUT -> "LOCKOUT STATE  ·  VISUAL PREVIEW"
            MODE_FLASHLIGHT -> "FLASHLIGHT STATE  ·  VISUAL PREVIEW"
            else -> "LIVE NATIVE RENDERER"
        }
    }
}
