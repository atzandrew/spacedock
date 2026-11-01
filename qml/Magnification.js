.pragma library

// Preserve the chosen size when it fits; otherwise shrink the row down to a
// usable minimum, after which the viewport remains scrollable.
function fitIconSize(count, size, spacing, padding, screenWidth, minimum) {
    if (count <= 0 || size <= 0) return size
    const fullWidth = count * size + Math.max(0, count - 1) * spacing + 2 * padding
    if (fullWidth <= screenWidth) return size
    return Math.min(size, Math.max(minimum, size * Math.max(0, screenWidth - 2) / fullWidth))
}

// Require enough space for the full configured falloff at the most crowded
// focus point, so the effect is consistent across the row.
function canMagnify(count, size, spacing, padding, screenWidth, scales) {
    if (count <= 0) return true
    let extra = 0
    for (const focus of [Math.floor((count - 1) / 2), (count - 1) / 2]) {
        let total = 0
        for (let i = 0; i < count; ++i)
            total += size * (sample(scales, Math.abs(i - focus)) - 1)
        extra = Math.max(extra, total)
    }
    return count * size + Math.max(0, count - 1) * spacing + 2 * padding + extra <= screenWidth + 0.001
}

// Piecewise-linear interpolation of the provisional observations in tuning.json.
// No frame counters or hidden easing constants: equal elapsed time gives equal progress.
function sample(values, position) {
    const x = Math.max(0, Math.min(values.length - 1, position))
    const lower = Math.floor(x)
    const upper = Math.min(lower + 1, values.length - 1)
    return values[lower] + (values[upper] - values[lower]) * (x - lower)
}

function transition(elapsedMs, durationMs, values) {
    return sample(values, Math.min(1, Math.max(0, elapsedMs / durationMs)) * (values.length - 1))
}

function shape(count, baseSize, spacing, available, focus, amount, scales, gapIndex = -1, gapAmount = 1) {
    const sizes = []
    const occupied = count - (gapIndex >= 0 ? 1 - gapAmount : 0)
    const baseWidth = Math.max(0, occupied * baseSize + (occupied - 1) * spacing)
    let extra = 0
    for (let i = 0; i < count; ++i) {
        const position = i - (gapIndex >= 0 && i > gapIndex ? 1 - gapAmount : 0)
            - (i === gapIndex ? (1 - gapAmount) / 2 : 0)
        const size = baseSize * (1 + amount * (sample(scales, Math.abs(position - focus)) - 1))
        sizes.push(size)
        extra += (size - baseSize) * (i === gapIndex ? gapAmount : 1)
    }
    // Keep the normal scrolling layout when the resting dock already overflows.
    // Otherwise limit magnification to the available screen width.
    const fit = baseWidth > available ? 0 : extra > 0 ? Math.min(1, (available - baseWidth) / extra) : 1
    const items = []
    let x = 0
    for (let i = 0; i < count; ++i) {
        const size = baseSize + (sizes[i] - baseSize) * fit
        const weight = i === gapIndex ? gapAmount : 1
        const position = i - (gapIndex >= 0 && i > gapIndex ? 1 - gapAmount : 0)
            - (i === gapIndex ? (1 - gapAmount) / 2 : 0)
        items.push({x: x, size: size, weight: weight, position: position})
        x += (size + spacing) * weight
    }
    return {items: items, width: Math.max(0, count > 0 ? x - spacing : 0), focus: focus}
}

function anchor(layout, focus, pitch) {
    const items = layout.items.filter(item => item.weight > 0)
    if (items.length === 0) return 0
    const center = item => item.x + item.size * item.weight / 2
    const first = items[0]
    const last = items[items.length - 1]
    if (focus <= first.position) return center(first) + (focus - first.position) * pitch
    if (focus >= last.position) return center(last) + (focus - last.position) * pitch
    for (let i = 0; i + 1 < items.length; ++i) {
        const a = items[i]
        const b = items[i + 1]
        if (focus <= b.position)
            return center(a) + (center(b) - center(a)) * (focus - a.position) / (b.position - a.position)
    }
    return center(last)
}

function layout(count, baseSize, spacing, screenWidth, padding, pointerX, amount, scales, gapIndex = -1, gapAmount = 1) {
    const available = Math.max(0, screenWidth - padding * 2)
    if (count === 0) return {items: [], width: 0, focus: -1}
    const rest = shape(count, baseSize, spacing, available, 0, 0, scales, gapIndex, gapAmount)
    if (amount <= 0 || rest.width >= available) return rest
    // Invert the expanded geometry so the magnification peak follows the pointer
    // without chasing positions fed back from moving icon delegates.
    const radius = scales.length - 1
    let low = -radius
    let high = count - 1 - (gapIndex >= 0 ? 1 - gapAmount : 0) + radius
    for (let iteration = 0; iteration < 20; ++iteration) {
        const focus = (low + high) / 2
        const current = shape(count, baseSize, spacing, available, focus, amount, scales, gapIndex, gapAmount)
        const pointer = (screenWidth - current.width) / 2 + anchor(current, focus, baseSize + spacing)
        if (pointer < pointerX) low = focus
        else high = focus
    }
    return shape(count, baseSize, spacing, available, (low + high) / 2, amount, scales, gapIndex, gapAmount)
}

// Interpolate the entire compact row into its parted layout in one pass.
// Changing a zero-width insertion slot cannot move any visible application.
function insertionLayout(count, baseSize, spacing, screenWidth, padding, pointerX, amount, scales, gapIndex, gapAmount) {
    const full = layout(count, baseSize, spacing, screenWidth, padding, pointerX, amount, scales)
    if (gapIndex < 0 || count === 0) return full
    const compact = layout(count - 1, baseSize, spacing, screenWidth, padding, pointerX, amount, scales)
    const items = full.items.map((item, slot) => {
        const rank = slot < gapIndex ? slot : slot - 1
        const closed = slot === gapIndex
            ? {x: compact.items[slot] ? compact.items[slot].x : compact.width, size: item.size}
            : compact.items[rank]
        return {x: closed.x + (item.x - closed.x) * gapAmount,
            size: closed.size + (item.size - closed.size) * gapAmount,
            weight: slot === gapIndex ? gapAmount : 1}
    })
    return {items: items, width: compact.width + (full.width - compact.width) * gapAmount}
}

// Magnification treats the row as a continuous hover surface. Bridge only the
// gaps between adjacent icons; keep the empty space around the dock click-through.
function hoverRects(items, offsetX, baseline, clipLeft, clipRight) {
    const rectangles = []
    function append(left, right, size) {
        left = Math.max(clipLeft, left)
        right = Math.min(clipRight, right)
        if (right > left)
            rectangles.push({x: left, y: baseline - size, width: right - left, height: size})
    }
    for (let i = 0; i < items.length; ++i) {
        const item = items[i]
        const extent = item.size * (item.weight === undefined ? 1 : item.weight)
        if (extent > 0) append(offsetX + item.x, offsetX + item.x + extent, item.size)
        if (i + 1 < items.length) {
            const next = items[i + 1]
            append(offsetX + item.x + extent, offsetX + next.x, Math.max(item.size, next.size))
        }
    }
    return rectangles
}
