import sharp from "sharp";
import { bounds, despill, register } from "./refinement.js";

const limitInputPixels = 16 * 1024 * 1024;
export async function decodePng(source: Buffer) {
    if (source.length > 36 * 1024 * 1024) throw new Error("Sprite image exceeds 36 MiB.");
    const decoder = sharp(source, { limitInputPixels, failOn: "warning" });
    const metadata = await decoder.metadata();
    if (metadata.format !== "png" || (metadata.pages ?? 1) !== 1) throw new Error("Sprite source must be a single PNG image.");
    return decoder.toColourspace("srgb").ensureAlpha().raw().toBuffer({ resolveWithObject: true });
}

/** Shared coordinates and scale preserve motion; never independently center each pose. */
export async function extractRow(source: Buffer, count: number, cellSize: number, chroma: string, tolerance: number,
    options: { despill?: boolean; align?: boolean } = {}) {
    const { data, info } = await decodePng(source);
    if (info.width % count || info.width / count < 4 || info.height < 4) {
        throw new Error(`Image width must divide into exactly ${count} cells. Adjust the source layout before composing.`);
    }
    const width = info.width / count, height = info.height;
    const key = [1, 3, 5].map(offset => parseInt(chroma.slice(offset, offset + 2), 16));
    // Remove only edge-connected key colour, preserving enclosed details of the same colour.
    const visited = new Uint8Array(width * height);
    const queue = new Int32Array(width * height);
    const cells: Buffer[] = [];
    const baseline: Buffer[] = [];
    const cleanup = [];
    let left = width, top = height, right = -1, bottom = -1;
    const warnings: string[] = [];
    for (let frame = 0; frame < count; frame++) {
        const pixels = Buffer.alloc(width * height * 4);
        for (let y = 0; y < height; y++) data.copy(pixels, y * width * 4, (y * info.width + frame * width) * 4, (y * info.width + (frame + 1) * width) * 4);
        visited.fill(0);
        let head = 0, tail = 0;
        const enqueue = (index: number) => {
            if (visited[index]) return;
            visited[index] = 1;
            const p = index * 4;
            if (pixels[p + 3] > 0 && Math.max(...key.map((color, channel) => Math.abs(pixels[p + channel] - color))) > tolerance) return;
            queue[tail++] = index;
        };
        for (let x = 0; x < width; x++) { enqueue(x); enqueue((height - 1) * width + x); }
        for (let y = 0; y < height; y++) { enqueue(y * width); enqueue(y * width + width - 1); }
        while (head < tail) {
            const index = queue[head++], x = index % width, y = Math.floor(index / width);
            pixels.fill(0, index * 4, index * 4 + 4);
            if (x) enqueue(index - 1);
            if (x < width - 1) enqueue(index + 1);
            if (y) enqueue(index - width);
            if (y < height - 1) enqueue(index + width);
        }
        let visible = 0, touchesEdge = false;
        for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
            if (pixels[(y * width + x) * 4 + 3] <= 8) continue;
            visible++;
            touchesEdge ||= x === 0 || y === 0 || x === width - 1 || y === height - 1;
            left = Math.min(left, x); right = Math.max(right, x); top = Math.min(top, y); bottom = Math.max(bottom, y);
        }
        if (!visible) throw new Error(`Frame ${frame} is empty after background removal.`);
        if (touchesEdge) throw new Error(`Frame ${frame} touches its cell boundary; check background colour or overlapping/cropped poses.`);
        if (baseline.some(cell => cell.equals(pixels))) warnings.push(`Frame ${frame} duplicates an earlier frame.`);
        baseline.push(Buffer.from(pixels));
        cleanup.push(options.despill === false ? { edgePixels: 0, trappedPixels: 0 } : despill(pixels, width, height, key));
        cells.push(pixels);
    }
    const registration = register(cells, width, height, options.align !== false);
    for (const offset of registration.offsets) if (offset.status === "uncertain") warnings.push(`Frame ${offset.sourceIndex} upper-body alignment is uncertain; no shift applied.`);
    // Include both variants in one coordinate system for a scale-matched comparison.
    cells.forEach((pixels, index) => {
        const box = bounds(pixels, width, height), dx = registration.offsets[index].dx;
        left = Math.min(left, box.left + dx); right = Math.max(right, box.right + dx);
    });
    const crop = { left, top, width: right - left + 1, height: bottom - top + 1 };
    const padding = Math.max(1, Math.round(cellSize * 0.05));
    const scale = Math.min((cellSize - padding * 2) / crop.width, (cellSize - padding * 2) / crop.height);
    const outWidth = Math.max(1, Math.round(crop.width * scale)), outHeight = Math.max(1, Math.round(crop.height * scale));
    const render = async (pixels: Buffer, dx: number) => {
        const padded = Buffer.alloc(crop.width * crop.height * 4);
        for (let y = top; y <= bottom; y++) {
            const start = Math.max(0, left - dx), end = Math.min(width, left + crop.width - dx);
            if (end > start) pixels.copy(padded, ((y - top) * crop.width + start + dx - left) * 4,
                (y * width + start) * 4, (y * width + end) * 4);
        }
        const content = await sharp(padded, { raw: { width: crop.width, height: crop.height, channels: 4 } })
            .resize(outWidth, outHeight, { kernel: "nearest" }).png().toBuffer();
        return sharp({ create: { width: cellSize, height: cellSize, channels: 4, background: "#00000000" } })
            .composite([{ input: content, left: Math.floor((cellSize - outWidth) / 2), top: cellSize - padding - outHeight }]).png().toBuffer();
    };
    const frames: Buffer[] = [], beforeFrames: Buffer[] = [];
    for (let index = 0; index < cells.length; index++) {
        frames.push(await render(cells[index], registration.offsets[index].dx));
        beforeFrames.push(await render(baseline[index], 0));
    }
    return { frames, beforeFrames, warnings, sourceSize: { width: info.width, height: info.height }, crop,
        cleanup, registration: { ...registration, offsets: registration.offsets.map(offset => ({ ...offset, outputDx: offset.dx * scale })) } };
}

export async function layoutGuide(count: number, chroma: string) {
    const { width, height } = rowDimensions(count);
    // A row occupies the whole image. Cells can be tall; a long strip isn't required by the provider.
    const lines = Array.from({ length: count - 1 }, (_, index) =>
        `<line x1="${width * (index + 1) / count}" y1="0" x2="${width * (index + 1) / count}" y2="${height}" stroke="#555"/>`).join("");
    return sharp(Buffer.from(`<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}"><rect width="100%" height="100%" fill="${chroma}"/>${lines}</svg>`)).png().toBuffer();
}

export function rowDimensions(count: number) {
    return { width: count * 16 * Math.ceil(1536 / (count * 16)), height: 1024 };
}
