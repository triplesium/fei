import sharp from "sharp";
import { bounds, keyTint } from "./refinement.js";

/** Heuristics report suspicious output; they do not validate anatomy or gait. */
export async function inspectFrames(frames: Buffer[], chroma: string, loop: boolean) {
    const key = [1, 3, 5].map(offset => parseInt(chroma.slice(offset, offset + 2), 16));
    const decoded = await Promise.all(frames.map(frame => sharp(frame).ensureAlpha().raw().toBuffer({ resolveWithObject: true })));
    const thumbnails = await Promise.all(frames.map(frame => sharp(frame).resize(32, 32).ensureAlpha().raw().toBuffer()));
    const hashes = await Promise.all(frames.map(async frame => {
        const image = await sharp(frame).flatten({ background: "#808080" }).resize(9, 8).greyscale().raw().toBuffer();
        return Array.from({ length: 64 }, (_, i) => image[Math.floor(i / 8) * 9 + i % 8] > image[Math.floor(i / 8) * 9 + i % 8 + 1] ? "1" : "0").join("");
    }));
    const metrics = decoded.map(({ data, info }, index) => {
        const box = bounds(data, info.width, info.height);
        let visiblePixels = 0, keyTintPixels = 0, weight = 0, sum = 0, upperWeight = 0, upperSum = 0;
        const upperBottom = box.top + (box.bottom - box.top + 1) * 0.35;
        for (let y = 0; y < info.height; y++) for (let x = 0; x < info.width; x++) {
            const p = (y * info.width + x) * 4, alpha = data[p + 3] / 255;
            if (data[p + 3] <= 8) continue;
            visiblePixels++; weight += alpha; sum += x * alpha;
            if (y < upperBottom) { upperWeight += alpha; upperSum += x * alpha; }
            if (keyTint(data, p, key) > 0.15) keyTintPixels++;
        }
        return { index, bounds: box, visiblePixels, keyTintPixels, centroidX: weight ? sum / weight : 0,
            upperCentroidX: upperWeight ? upperSum / upperWeight : 0, dHash: hashes[index] };
    });
    const transitions = Array.from({ length: Math.max(0, frames.length - (loop ? 0 : 1)) }, (_, index) => {
        const next = (index + 1) % frames.length, a = thumbnails[index], b = thumbnails[next];
        let difference = 0, weight = 0, silhouette = 0;
        for (let p = 0; p < a.length; p += 4) {
            const aa = a[p + 3] / 255, ba = b[p + 3] / 255;
            weight += Math.max(aa, ba); silhouette += Math.abs(aa - ba);
            for (let c = 0; c < 3; c++) difference += Math.abs(a[p + c] * aa - b[p + c] * ba) / 255;
        }
        const visualDifference = weight ? (difference / 3 + silhouette) / (2 * weight) : 0;
        const hashDistance = [...hashes[index]].filter((bit, i) => bit !== hashes[next][i]).length;
        return { from: index, to: next, visualDifference, silhouetteDifference: weight ? silhouette / weight : 0,
            hashDistance, nearDuplicate: visualDifference < 0.035 || (hashDistance <= 6 && visualDifference < 0.12) };
    });
    const centers = metrics.map(frame => frame.upperCentroidX);
    const horizontalDrift = Math.max(...centers) - Math.min(...centers);
    const meanMotion = transitions.length ? transitions.reduce((sum, item) => sum + item.visualDifference, 0) / transitions.length : 0;
    const warnings: string[] = [];
    if (frames.length < 2) warnings.push("Only one playback frame; motion cannot be assessed.");
    if (horizontalDrift > Math.max(2, decoded[0].info.width * 0.03)) warnings.push("Upper-body horizontal drift is high; review registration or intentional sideways motion.");
    if (frames.length > 1 && meanMotion < 0.06) warnings.push("Low motion: poses may be too similar. Verify the requested action visually.");
    if (transitions.some(item => item.nearDuplicate)) warnings.push("Near-duplicate neighbouring frames; check holds and limb alternation.");
    if (metrics.some(frame => frame.keyTintPixels > Math.max(2, frame.visiblePixels * 0.005))) warnings.push("Possible background colour spill remains; check against intentional character colours.");
    const heights = metrics.map(frame => frame.bounds.bottom - frame.bounds.top + 1);
    if (Math.max(...heights) > Math.min(...heights) * 1.25) warnings.push("Frame height varies by more than 25%; check scale or intentional deformation.");
    const interior = transitions.slice(0, -1);
    const interiorMean = interior.length ? interior.reduce((sum, item) => sum + item.visualDifference, 0) / interior.length : 0;
    const seamRatio = loop && interior.length ? transitions.at(-1)!.visualDifference / Math.max(0.01, interiorMean) : null;
    if (seamRatio !== null && seamRatio > 2.5) warnings.push("Loop seam changes much more than interior transitions.");
    return { status: warnings.length ? "review" : "no-heuristic-flags", requiresVisualReview: true,
        thresholds: { lowMotion: 0.06, nearDuplicateDifference: 0.035, similarHashDistance: 6, similarHashDifference: 0.12,
            horizontalDrift: Math.max(2, decoded[0].info.width * 0.03), seamRatio: 2.5 },
        horizontalDrift, meanMotion, seamRatio, frames: metrics, transitions, warnings };
}
