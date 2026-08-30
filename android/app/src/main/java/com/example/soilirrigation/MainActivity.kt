package com.example.soilirrigation

import android.os.Bundle
import android.widget.*
import androidx.appcompat.app.AppCompatActivity
import org.json.JSONArray
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.Executors

class MainActivity : AppCompatActivity() {
    private val executor = Executors.newSingleThreadExecutor()

    private lateinit var baseUrl: EditText
    private lateinit var connectionStatus: TextView
    private lateinit var zone1Status: TextView
    private lateinit var zone2Status: TextView
    private lateinit var log: TextView

    private lateinit var auto1: CheckBox
    private lateinit var auto2: CheckBox
    private lateinit var start1: EditText
    private lateinit var start2: EditText
    private lateinit var stop1: EditText
    private lateinit var stop2: EditText
    private lateinit var max1: EditText
    private lateinit var max2: EditText
    private lateinit var sched1: EditText
    private lateinit var sched2: EditText
    private lateinit var dur1: EditText
    private lateinit var dur2: EditText

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        baseUrl = findViewById(R.id.baseUrl)
        connectionStatus = findViewById(R.id.connectionStatus)
        zone1Status = findViewById(R.id.zone1Status)
        zone2Status = findViewById(R.id.zone2Status)
        log = findViewById(R.id.log)

        auto1 = findViewById(R.id.auto1)
        auto2 = findViewById(R.id.auto2)
        start1 = findViewById(R.id.start1)
        start2 = findViewById(R.id.start2)
        stop1 = findViewById(R.id.stop1)
        stop2 = findViewById(R.id.stop2)
        max1 = findViewById(R.id.max1)
        max2 = findViewById(R.id.max2)
        sched1 = findViewById(R.id.sched1)
        sched2 = findViewById(R.id.sched2)
        dur1 = findViewById(R.id.dur1)
        dur2 = findViewById(R.id.dur2)

        val prefs = getSharedPreferences("soil_irrigation", MODE_PRIVATE)
        baseUrl.setText(prefs.getString("base_url", "http://192.168.1.70"))

        findViewById<Button>(R.id.refresh).setOnClickListener { refreshAll() }
        findViewById<Button>(R.id.valve1On).setOnClickListener { setValve(1, true) }
        findViewById<Button>(R.id.valve1Off).setOnClickListener { setValve(1, false) }
        findViewById<Button>(R.id.valve2On).setOnClickListener { setValve(2, true) }
        findViewById<Button>(R.id.valve2Off).setOnClickListener { setValve(2, false) }
        findViewById<Button>(R.id.saveConfig).setOnClickListener { saveConfig() }
    }

    private fun root(): String =
        baseUrl.text.toString().trim().trimEnd('/')

    private fun request(path: String, method: String = "GET", body: String? = null): String {
        val conn = URL(root() + path).openConnection() as HttpURLConnection
        conn.requestMethod = method
        conn.connectTimeout = 3000
        conn.readTimeout = 5000
        conn.setRequestProperty("Accept", "application/json")
        if (body != null) {
            conn.doOutput = true
            conn.setRequestProperty("Content-Type", "application/json")
            conn.outputStream.use { it.write(body.toByteArray()) }
        }

        val code = conn.responseCode
        val stream = if (code in 200..299) conn.inputStream else conn.errorStream
        val text = stream.bufferedReader().use { it.readText() }
        if (code !in 200..299) throw RuntimeException("HTTP $code: $text")
        return text
    }

    private fun refreshAll() {
        getSharedPreferences("soil_irrigation", MODE_PRIVATE).edit()
            .putString("base_url", root()).apply()

        executor.execute {
            try {
                val status = JSONObject(request("/api/status"))
                val config = JSONObject(request("/api/config"))
                runOnUiThread {
                    renderStatus(status)
                    renderConfig(config)
                    connectionStatus.text = "Connected"
                    log.text = "Updated successfully"
                }
            } catch (e: Exception) {
                runOnUiThread {
                    connectionStatus.text = "Connection failed"
                    log.text = e.message
                }
            }
        }
    }

    private fun renderStatus(root: JSONObject) {
        val sensors = root.getJSONArray("sensors")
        val valves = root.getJSONArray("valves")

        fun zoneText(index: Int): String {
            val s = sensors.getJSONObject(index)
            val v = valves.getJSONObject(index)
            val valid = s.optBoolean("valid", false)
            val moisture = if (s.has("moisturePct")) "%.1f%%".format(s.getDouble("moisturePct")) else "--"
            val temp = if (s.has("temperatureC")) "%.1f°C".format(s.getDouble("temperatureC")) else "--"
            val ec = s.optInt("ec", 0)
            val ph = if (s.has("ph")) "%.1f".format(s.getDouble("ph")) else "--"
            val valve = if (v.optBoolean("on")) "ON (${v.optString("reason")})" else "OFF"
            return "Zone ${index + 1}: sensor=${if (valid) "OK" else "ERROR"}\n" +
                    "Moisture $moisture   Temp $temp   EC $ec   pH $ph\nValve $valve"
        }

        zone1Status.text = zoneText(0)
        zone2Status.text = zoneText(1)
    }

    private fun renderConfig(root: JSONObject) {
        val zones = root.getJSONArray("zones")
        renderZoneConfig(zones.getJSONObject(0), 1)
        renderZoneConfig(zones.getJSONObject(1), 2)
    }

    private fun renderZoneConfig(z: JSONObject, id: Int) {
        val auto = if (id == 1) auto1 else auto2
        val start = if (id == 1) start1 else start2
        val stop = if (id == 1) stop1 else stop2
        val max = if (id == 1) max1 else max2
        val sched = if (id == 1) sched1 else sched2
        val dur = if (id == 1) dur1 else dur2

        auto.isChecked = z.optBoolean("autoEnabled", true)
        start.setText(z.optDouble("startBelowPct", 28.0).toString())
        stop.setText(z.optDouble("stopAbovePct", 36.0).toString())
        max.setText(z.optInt("maxRunMinutes", 20).toString())

        val schedules = z.optJSONArray("schedules") ?: JSONArray()
        if (schedules.length() > 0) {
            val s = schedules.getJSONObject(0)
            if (s.optBoolean("enabled", false)) {
                val minutes = s.optInt("startMinute", 420)
                sched.setText("%02d:%02d".format(minutes / 60, minutes % 60))
            } else {
                sched.setText("")
            }
            dur.setText(s.optInt("durationMinutes", 10).toString())
        }
    }

    private fun timeToMinute(text: String): Int? {
        if (text.isBlank()) return null
        val parts = text.trim().split(":")
        if (parts.size != 2) return null
        val h = parts[0].toIntOrNull() ?: return null
        val m = parts[1].toIntOrNull() ?: return null
        if (h !in 0..23 || m !in 0..59) return null
        return h * 60 + m
    }

    private fun zoneJson(
        id: Int,
        auto: CheckBox,
        start: EditText,
        stop: EditText,
        max: EditText,
        sched: EditText,
        dur: EditText
    ): JSONObject {
        val scheduledMinute = timeToMinute(sched.text.toString())
        val schedule = JSONObject()
            .put("enabled", scheduledMinute != null)
            .put("daysMask", 127) // every day in this simple v1 UI
            .put("startMinute", scheduledMinute ?: 420)
            .put("durationMinutes", dur.text.toString().toIntOrNull() ?: 10)

        return JSONObject()
            .put("id", id)
            .put("autoEnabled", auto.isChecked)
            .put("startBelowPct", start.text.toString().toDoubleOrNull() ?: 28.0)
            .put("stopAbovePct", stop.text.toString().toDoubleOrNull() ?: 36.0)
            .put("maxRunMinutes", max.text.toString().toIntOrNull() ?: 20)
            .put("schedules", JSONArray().put(schedule))
    }

    private fun saveConfig() {
        val body = JSONObject()
            .put("zones", JSONArray()
                .put(zoneJson(1, auto1, start1, stop1, max1, sched1, dur1))
                .put(zoneJson(2, auto2, start2, stop2, max2, sched2, dur2))
            )

        executor.execute {
            try {
                request("/api/config", "POST", body.toString())
                runOnUiThread {
                    Toast.makeText(this, "Configuration saved", Toast.LENGTH_SHORT).show()
                    refreshAll()
                }
            } catch (e: Exception) {
                runOnUiThread { log.text = e.message }
            }
        }
    }

    private fun setValve(id: Int, on: Boolean) {
        executor.execute {
            try {
                request("/api/valve?id=$id&on=$on", "POST")
                runOnUiThread { refreshAll() }
            } catch (e: Exception) {
                runOnUiThread { log.text = e.message }
            }
        }
    }

    override fun onDestroy() {
        executor.shutdownNow()
        super.onDestroy()
    }
}
