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

// The inverse search needs only the pointer coordinate, not delegate objects.
// Accumulate the same sizes and anchor as shape()/anchor() without allocating
// the provisional item array on every bisection step.
function pointerAtFocus(count, baseSize, spacing, available, screenWidth, focus, amount, scales, gapIndex, gapAmount) {
    const closed = gapIndex >= 0 ? 1 - gapAmount : 0
    const occupied = count - closed
    const baseWidth = Math.max(0, occupied * baseSize + (occupied - 1) * spacing)
    let extra = 0
    let x = 0
    let previousPosition = 0
    let previousBase = 0
    let previousExtra = 0
    let havePrevious = false
    let found = false
    let anchorBase = 0
    let anchorExtra = 0
    for (let i = 0; i < count; ++i) {
        const position = i - (i > gapIndex && gapIndex >= 0 ? closed : 0)
            - (i === gapIndex ? closed / 2 : 0)
        const growth = baseSize * amount * (sample(scales, Math.abs(position - focus)) - 1)
        const weight = i === gapIndex ? gapAmount : 1
        if (weight > 0) {
            const centerBase = x + baseSize * weight / 2
            const centerExtra = extra + growth * weight / 2
            if (!found && focus <= position) {
                if (havePrevious) {
                    const fraction = (focus - previousPosition) / (position - previousPosition)
                    anchorBase = previousBase + (centerBase - previousBase) * fraction
                    anchorExtra = previousExtra + (centerExtra - previousExtra) * fraction
                } else {
                    anchorBase = centerBase + (focus - position) * (baseSize + spacing)
                    anchorExtra = centerExtra
                }
                found = true
            }
            previousPosition = position
            previousBase = centerBase
            previousExtra = centerExtra
            havePrevious = true
        }
        x += (baseSize + spacing) * weight
        extra += growth * weight
    }
    if (!found && havePrevious) {
        anchorBase = previousBase + (focus - previousPosition) * (baseSize + spacing)
        anchorExtra = previousExtra
    }
    const fit = baseWidth > available ? 0 : extra > 0 ? Math.min(1, (available - baseWidth) / extra) : 1
    return (screenWidth - Math.max(0, x - spacing + extra * fit)) / 2 + anchorBase + anchorExtra * fit
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
    const origin = low
    const end = high
    const cellWidth = (end - origin) / 1048576 // the original 20-step search grid
    let lowPointer = pointerAtFocus(count, baseSize, spacing, available, screenWidth, low, amount, scales, gapIndex, gapAmount)
    let highPointer = pointerAtFocus(count, baseSize, spacing, available, screenWidth, high, amount, scales, gapIndex, gapAmount)
    if (pointerX <= lowPointer)
        return shape(count, baseSize, spacing, available, low + cellWidth / 2, amount, scales, gapIndex, gapAmount)
    if (pointerX >= highPointer)
        return shape(count, baseSize, spacing, available, high - cellWidth / 2, amount, scales, gapIndex, gapAmount)
    // Find the inverse quickly, then validate its cell on the original search
    // grid. Keeping that cell's midpoint preserves pointer hit-test boundaries.
    for (let iteration = 0; iteration < 8; ++iteration) {
        const fraction = Math.max(0.1, Math.min(0.9, (pointerX - lowPointer) / (highPointer - lowPointer)))
        const focus = low + (high - low) * fraction
        const pointer = pointerAtFocus(count, baseSize, spacing, available, screenWidth, focus, amount, scales, gapIndex, gapAmount)
        if (Math.abs(pointer - pointerX) <= 0.000001) {
            let cellLow = origin + Math.floor((focus - origin) / cellWidth) * cellWidth
            let cellPointer = pointerAtFocus(count, baseSize, spacing, available, screenWidth, cellLow, amount, scales, gapIndex, gapAmount)
            if (pointerX <= cellPointer) {
                cellLow -= cellWidth
                cellPointer = pointerAtFocus(count, baseSize, spacing, available, screenWidth, cellLow, amount, scales, gapIndex, gapAmount)
            }
            const cellHighPointer = pointerAtFocus(count, baseSize, spacing, available, screenWidth, cellLow + cellWidth, amount, scales, gapIndex, gapAmount)
            if (cellLow >= origin && cellLow + cellWidth <= end && cellPointer < pointerX && pointerX <= cellHighPointer)
                return shape(count, baseSize, spacing, available, cellLow + cellWidth / 2, amount, scales, gapIndex, gapAmount)
            break
        }
        if (pointer < pointerX) {
            low = focus
            lowPointer = pointer
        } else {
            high = focus
            highPointer = pointer
        }
    }
    // Unusual curves retain the original search and its precision.
    low = origin
    high = end
    for (let iteration = 0; iteration < 20; ++iteration) {
        const focus = (low + high) / 2
        const pointer = pointerAtFocus(count, baseSize, spacing, available, screenWidth, focus, amount, scales, gapIndex, gapAmount)
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
