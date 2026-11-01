import QtQuick

// Original procedural artwork. Static geometry is estimated from the private
// reference crop (see tuning.json); colors remain provisional.
Canvas {
    id: shelf
    required property real inset
    required property real lip
    readonly property real rearInset: Math.min(inset, width / 4)
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onInsetChanged: requestPaint()
    onLipChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const front = height - lip
        ctx.beginPath()
        ctx.moveTo(rearInset, 0.5)
        ctx.lineTo(width - rearInset, 0.5)
        ctx.lineTo(width - 0.5, front)
        ctx.lineTo(0.5, front)
        ctx.closePath()
        const glass = ctx.createLinearGradient(0, 0, 0, front)
        glass.addColorStop(0, "#778b9aa7")
        glass.addColorStop(0.35, "#997e8c98")
        glass.addColorStop(1, "#c0bdc8d0")
        ctx.fillStyle = glass
        ctx.fill()
        ctx.strokeStyle = "#a8edf4fa"
        ctx.lineWidth = 1
        ctx.stroke()
        const edge = ctx.createLinearGradient(0, front, 0, height)
        edge.addColorStop(0, "#e6e7eef3")
        edge.addColorStop(0.3, "#b09baab7")
        edge.addColorStop(1, "#e63b4651")
        ctx.fillStyle = edge
        ctx.fillRect(0, front, width, lip)
        ctx.fillStyle = "#90303b45"
        ctx.fillRect(0, height - 1, width, 1)
    }
}
