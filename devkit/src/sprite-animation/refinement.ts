export type Bounds = { left: number; top: number; right: number; bottom: number };

export function bounds(pixels: Buffer, width: number, height: number): Bounds {
    const box = { left: width, top: height, right: -1, bottom: -1 };
    for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
        if (pixels[(y * width + x) * 4 + 3] <= 8) continue;
        box.left = Math.min(box.left, x); box.right = Math.max(box.right, x);
        box.top = Math.min(box.top, y); box.bottom = Math.max(box.bottom, y);
    }
    return box;
}

/** Estimate excess key chroma. Neutral/low-saturation keys cannot be unmixed this way. */
export function keyTint(pixels: Buffer, p: number, key: number[]) {
    const high = Math.max(...key), low = Math.min(...key), spread = high - low;
    if (spread < 96) return 0;
    let upper = 255, lower = 0;
    for (let c = 0; c < 3; c++) {
        if (key[c] >= high - spread * 0.2) upper = Math.min(upper, pixels[p + c]);
        if (key[c] <= low + spread * 0.2) lower = Math.max(lower, pixels[p + c]);
    }
    return Math.max(0, Math.min(0.98, (upper - lower) / spread));
}

/** Bounded edge unmix plus small interior spill clusters; never erode the silhouette. */
export function despill(pixels: Buffer, width: number, height: number, key: number[]) {
    const size = width * height, distance = new Uint8Array(size).fill(255);
    const queue = new Int32Array(size);
    let head = 0, tail = 0, visible = 0, edgePixels = 0, trappedPixels = 0;
    const neighbours = (i: number, visit: (j: number) => void) => {
        if (i % width) visit(i - 1);
        if (i % width < width - 1) visit(i + 1);
        if (i >= width) visit(i - width);
        if (i < size - width) visit(i + width);
    };
    for (let i = 0; i < size; i++) {
        if (!pixels[i * 4 + 3]) { distance[i] = 0; queue[tail++] = i; }
        else visible++;
    }
    while (head < tail) {
        const i = queue[head++];
        if (distance[i] >= 4) continue;
        neighbours(i, j => {
            if (distance[j] !== 255) return;
            distance[j] = distance[i] + 1; queue[tail++] = j;
        });
    }
    const unmix = (i: number, tint: number, soft: boolean) => {
        const p = i * 4;
        for (let c = 0; c < 3; c++) pixels[p + c] = Math.round(Math.max(0, Math.min(255, (pixels[p + c] - tint * key[c]) / (1 - tint))));
        if (soft) pixels[p + 3] = Math.round(pixels[p + 3] * (1 - tint));
        if (!pixels[p + 3]) pixels.fill(0, p, p + 4);
    };
    // Determine clusters before altering edge colours: large intentional key-coloured
    // materials must not become artificial small islands as a side effect of cleanup.
    const visited = new Uint8Array(size);
    for (let i = 0; i < size; i++) {
        if (visited[i] || !pixels[i * 4 + 3] || keyTint(pixels, i * 4, key) <= 0.08) continue;
        head = 0; tail = 1; queue[0] = i; visited[i] = 1;
        let strong = false, interior = true;
        while (head < tail) {
            const j = queue[head++];
            strong ||= keyTint(pixels, j * 4, key) >= 0.35;
            interior &&= distance[j] > 4;
            neighbours(j, k => {
                if (visited[k] || !pixels[k * 4 + 3] || keyTint(pixels, k * 4, key) <= 0.08) return;
                visited[k] = 1; queue[tail++] = k;
            });
        }
        if (interior && strong && tail <= Math.max(1, Math.floor(visible * 0.005))) {
            for (let j = 0; j < tail; j++) unmix(queue[j], keyTint(pixels, queue[j] * 4, key), false);
            trappedPixels += tail;
        }
    }
    for (let i = 0; i < size; i++) {
        if (!pixels[i * 4 + 3] || distance[i] > 4) continue;
        const tint = keyTint(pixels, i * 4, key);
        if (tint <= 0.08) continue;
        unmix(i, tint, true); edgePixels++;
    }
    return { edgePixels, trappedPixels };
}

/** Register upper-body masks in source coordinates. Vertical slack is used only
 * for matching, never applied to the output, so jumps and body bob survive. */
export function register(cells: Buffer[], width: number, height: number, enabled: boolean) {
    const boxes = cells.map(cell => bounds(cell, width, height));
    const sampleWidth = Math.min(128, width), sampleHeight = 32;
    const step = width / sampleWidth;
    const upperHeight = Math.max(1, [...boxes].map(b => (b.bottom - b.top + 1) * 0.35).sort((a, b) => a - b)[Math.floor(boxes.length / 2)]);
    const masks = cells.map((cell, index) => {
        const mask = new Float32Array(sampleWidth * sampleHeight);
        for (let y = 0; y < sampleHeight; y++) for (let x = 0; x < sampleWidth; x++) {
            const sy = Math.min(height - 1, boxes[index].top + Math.floor(y * upperHeight / sampleHeight));
            mask[y * sampleWidth + x] = cell[(sy * width + Math.floor(x * step)) * 4 + 3] / 255;
        }
        return mask;
    });
    const centers = masks.map(mask => {
        let weight = 0, sum = 0;
        for (let i = 0; i < mask.length; i++) { weight += mask[i]; sum += (i % sampleWidth) * mask[i]; }
        return weight ? sum / weight : 0;
    });
    const referenceIndex = centers.map((center, index) => ({ center, index })).sort((a, b) => a.center - b.center)[Math.floor(cells.length / 2)].index;
    const reference = masks[referenceIndex], limit = Math.max(1, Math.floor(sampleWidth * 0.15));
    const offsets = masks.map((mask, index) => {
        const score = (dx: number) => {
            let best = 0;
            for (let dy = -2; dy <= 2; dy++) {
                let intersection = 0, union = 0;
                for (let y = 2; y < sampleHeight - 2; y++) for (let x = -limit; x < sampleWidth + limit; x++) {
                    const a = x >= 0 && x < sampleWidth ? reference[y * sampleWidth + x] : 0;
                    const sx = x - dx;
                    const b = sx >= 0 && sx < sampleWidth ? mask[(y + dy) * sampleWidth + sx] : 0;
                    intersection += Math.min(a, b); union += Math.max(a, b);
                }
                best = Math.max(best, union ? intersection / union : 0);
            }
            return best;
        };
        const before = score(0);
        let best = before, shift = 0;
        if (enabled && index !== referenceIndex) for (let dx = -limit; dx <= limit; dx++) {
            const candidate = score(dx);
            if (candidate > best + 1e-6 || (Math.abs(candidate - best) < 1e-6 && Math.abs(dx) < Math.abs(shift))) { best = candidate; shift = dx; }
        }
        const reliable = best >= 0.5 && best - before >= 0.01 && Math.abs(shift) < limit;
        return { sourceIndex: index, dx: reliable ? Math.round(shift * step) : 0, dy: 0,
            overlapBefore: before, overlapAfter: reliable ? best : before,
            status: !enabled ? "disabled" : index === referenceIndex ? "reference" : reliable ? "aligned" : before >= 0.8 ? "unchanged" : "uncertain" };
    });
    return { referenceIndex, offsets };
}
