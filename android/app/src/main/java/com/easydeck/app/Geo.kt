package com.easydeck.app

import kotlin.math.*

/** Small geo helpers for the people radar (pure, unit-tested). */
object Geo {
    private const val R = 6371000.0
    fun distM(la1: Double, lo1: Double, la2: Double, lo2: Double): Double {
        val dla = Math.toRadians(la2 - la1); val dlo = Math.toRadians(lo2 - lo1)
        val a = sin(dla / 2).pow(2) + cos(Math.toRadians(la1)) * cos(Math.toRadians(la2)) * sin(dlo / 2).pow(2)
        return 2 * R * atan2(sqrt(a), sqrt(1 - a))
    }
    /** Bearing from 1 to 2 in degrees, 0 = north, clockwise. */
    fun bearing(la1: Double, lo1: Double, la2: Double, lo2: Double): Double {
        val y = sin(Math.toRadians(lo2 - lo1)) * cos(Math.toRadians(la2))
        val x = cos(Math.toRadians(la1)) * sin(Math.toRadians(la2)) - sin(Math.toRadians(la1)) * cos(Math.toRadians(la2)) * cos(Math.toRadians(lo2 - lo1))
        return (Math.toDegrees(atan2(y, x)) + 360.0) % 360.0
    }
    /** A round ring distance (m) so the farthest person sits inside the outer ring. */
    fun ringStep(maxM: Double): Double {
        val steps = doubleArrayOf(50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0, 50000.0, 100000.0, 200000.0, 500000.0)
        for (s in steps) if (s * 3 >= maxM) return s
        return steps.last()
    }
    fun fmt(m: Double): String = if (m < 1000) "${m.roundToInt()} m" else if (m < 10000) "%.1f km".format(m / 1000) else "${(m / 1000).roundToInt()} km"
}
