package com.superdro.app.serial

import com.superdro.app.model.MachineState
import org.json.JSONObject

object Protocol {

    fun parseStatus(json: String): MachineState? {
        return try {
            val obj = JSONObject(json)

            // Skip ACK messages
            if (obj.has("ack")) return null

            val pos = obj.getJSONObject("pos")
            MachineState(
                xPosMm = pos.getDouble("x").toFloat(),
                zPosMm = pos.getDouble("z").toFloat(),
                rpm = obj.getDouble("rpm").toFloat(),
                state = obj.optString("state", "idle"),
                feedHold = obj.optBoolean("fh", false)
            )
        } catch (_: Exception) {
            null
        }
    }

    fun zeroCommand(axis: String): String =
        """{"cmd":"zero","axis":"$axis"}"""

    fun presetCommand(axis: String, value: Float): String =
        """{"cmd":"preset","axis":"$axis","value":$value}"""

    fun configGetCommand(key: String): String =
        """{"cmd":"config_get","key":"$key"}"""

    fun configSetCommand(key: String, value: String): String =
        """{"cmd":"config_set","key":"$key","value":$value}"""

    fun configSaveCommand(): String =
        """{"cmd":"config_save"}"""

    fun configListCommand(): String =
        """{"cmd":"config_list"}"""
}
