import QtQuick
import "Magnification.js" as Curves

Item {
    id: bounce
    required property var settings
    property bool launchActive: false
    property bool attentionActive: false
    property real offset: 0
    property bool repeatMotion: false
    property int finalCycle: 1
    property string reason: ""
    property real cycleDurationMs: 1
    property real cycleHeight: 0
    property var cycleSamples: [0, 0, 0]
    readonly property bool playing: motion.running
    function stop() {
        motion.stop()
        offset = 0
        reason = ""
    }
    function start(kind, repeat) {
        if (!settings.bounceEnabled) { stop(); return }
        cycleDurationMs = settings.bounceDurationMs
        cycleHeight = settings.bounceHeight
        cycleSamples = settings.bounceSamples.slice()
        reason = kind
        repeatMotion = repeat
        finalCycle = 1
        offset = 0
        motion.restart()
    }
    function triggerLaunch() {
        // A click guarantees one complete cycle. It is not evidence that the
        // process is still loading, so only launchActive permits repetition.
        if (playing && reason === "launch") return
        start("launch", launchActive)
    }
    function finishLaunchCycle() {
        repeatMotion = false
        finalCycle = Math.max(1, Math.ceil(motion.elapsedTime * 1000 / cycleDurationMs))
    }
    onLaunchActiveChanged: {
        if (launchActive) {
            if (playing && reason === "launch") repeatMotion = true
            else start("launch", true)
        } else if (playing && reason === "launch") {
            // Let the current bounce land, including when startup completes
            // before the first frame. Do not snap the icon back mid-air.
            finishLaunchCycle()
        }
    }
    onAttentionActiveChanged: {
        if (launchActive || (playing && reason === "launch")) return
        if (attentionActive) start("attention", false)
        else if (reason === "attention") stop()
    }
    onSettingsChanged: {
        if (!settings.bounceEnabled) stop()
    }
    Component.onCompleted: {
        if (launchActive) start("launch", true)
        else if (attentionActive) start("attention", false)
    }
    FrameAnimation {
        id: motion
        onTriggered: {
            const elapsed = elapsedTime * 1000
            const duration = bounce.cycleDurationMs
            if (!bounce.repeatMotion && elapsed >= duration * bounce.finalCycle) { bounce.stop(); return }
            bounce.offset = bounce.cycleHeight * Curves.sample(bounce.cycleSamples,
                (elapsed % duration) / duration * (bounce.cycleSamples.length - 1))
        }
    }
}
