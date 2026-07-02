import QtQuick
import QtTest

import "../.." as App

TestCase {
    name: "AudioLevelMeter"
    when: windowShown

    Component {
        id: meterComponent

        App.AudioLevelMeter {
            width: 240
            labelText: "RX"
            level: 0.5
            peakLevel: 0.75
            active: false
        }
    }

    function test_normalizedState_clampsAndDerivesSegments() {
        const meter = createTemporaryObject(meterComponent, this)
        verify(meter !== null)

        compare(meter.clampUnit("bad"), 0.0)
        compare(meter.normalizedLevel, 0.5)
        compare(meter.normalizedPeakLevel, 0.75)
        compare(meter.litSegmentCount, 8)
        compare(meter.peakSegmentIndex, 11)

        meter.level = 1.8
        meter.peakLevel = -1
        compare(meter.normalizedLevel, 1.0)
        compare(meter.normalizedPeakLevel, 1.0)
        compare(meter.litSegmentCount, 16)
        compare(meter.peakSegmentIndex, 15)
    }

    function test_segmentColor_thresholds_matchExpectedBands() {
        const meter = createTemporaryObject(meterComponent, this)
        verify(meter !== null)

        compare(meter.activeSegmentColor(0), "#22c55e")
        compare(meter.activeSegmentColor(10), "#f59e0b")
        compare(meter.activeSegmentColor(15), "#dc2626")
        compare(meter.inactiveSegmentColor(0), "#bbf7d0")
        compare(meter.inactiveSegmentColor(10), "#fde68a")
        compare(meter.inactiveSegmentColor(15), "#fca5a5")
    }
}
