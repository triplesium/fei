import { afterEach, expect, it, vi } from "vitest";
import { mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import sharp from "sharp";
import { HostProjectService } from "../src/workspace/project-service.js";
import { SpriteAnimationService } from "../src/sprite-animation/service.js";
import { extractRow } from "../src/sprite-animation/processing.js";
import { spriteAnimationSchema } from "../src/contracts/sprite-animation.js";
import { despill } from "../src/sprite-animation/refinement.js";
import { inspectFrames } from "../src/sprite-animation/quality.js";

const cleanup: (() => Promise<void>)[] = [];
afterEach(async () => { for (const dispose of cleanup.splice(0)) await dispose(); });
const run = "assets/hero-run";
const request = { reference: "assets/hero.png", description: "A red square hero", cellSize: 32,
    states: [{ name: "jump", action: "Jump and land", poses: ["On ground", "In air"], fps: 6, loop: true }] };
async function row(empty = false, clipped = false) {
    const data = Buffer.alloc(32 * 16 * 4);
    for (let i = 0; i < data.length; i += 4) { data[i] = 255; data[i + 2] = 255; data[i + 3] = 255; }
    for (let frame = 0; frame < (empty ? 1 : 2); frame++) {
        const top = frame ? 3 : 8;
        for (let y = top; y < top + 4; y++) for (let x = clipped ? 0 : 5; x < 9; x++) {
            const p = (y * 32 + frame * 16 + x) * 4;
            data[p + 2] = 0;
        }
    }
    return sharp(data, { raw: { width: 32, height: 16, channels: 4 } }).png().toBuffer();
}
async function fixture() {
    const root = await mkdtemp(join(tmpdir(), "entisium-sprites-"));
    await writeFile(join(root, "project.yaml"), "name: Test\n");
    const project = new HostProjectService(root);
    cleanup.push(async () => { project.dispose(); await rm(root, { recursive: true, force: true }); });
    await project.writeNew(request.reference, await row());
    const generate = vi.fn(async (input, signal) => {
        signal?.throwIfAborted();
        await project.writeNew(input.path, await row());
        return { path: input.path, mimeType: "image/png" as const, width: 32, height: 16, bytes: 100, model: "test" };
    });
    const service = new SpriteAnimationService(project, () => generate);
    await service.invoke({ operation: "prepare", run, request });
    return { project, generate, service };
}

it("preserves references, generates once per state, and exports selected frames with exact rectangles", async () => {
    const { service, project, generate } = await fixture();
    const original = await project.read(`${run}/base.png`);
    await project.write(request.reference, "changed original");
    expect(await project.read(`${run}/base.png`)).toEqual(original);
    await service.invoke({ operation: "generate", run, state: "jump" });
    expect(generate).toHaveBeenCalledOnce();
    expect(generate.mock.calls[0][0]).toMatchObject({ references: [`${run}/base.png`, `${run}/guides/jump.png`], background: "opaque" });
    await expect(service.invoke({ operation: "generate", run, state: "jump" })).rejects.toThrow("already has");
    const output = await service.invoke({ operation: "compose", run, selections: [{ state: "jump", order: [1, 0, 1], fps: 10, loop: false }] });
    const manifest = JSON.parse((await project.read(output.paths[0]))!.toString());
    expect(manifest).toMatchObject({ width: 96, height: 32, origin: "top-left", image: "atlas.png" });
    expect(manifest.animations[0]).toMatchObject({ loop: false, fps: 10,
        frames: [{ x: 0, sourceIndex: 1, durationMs: 100 }, { x: 32, sourceIndex: 0 }, { x: 64, sourceIndex: 1 }] });
    const quality = JSON.parse((await project.read(output.paths[3]))!.toString());
    expect(quality.rows[0].order).toEqual([1, 0, 1]);
    expect(quality.rows[0].after.transitions).toHaveLength(2);
    expect(quality.rows[0].after.requiresVisualReview).toBe(true);
    expect(manifest.processing.rows[0].registration.offsets).toHaveLength(2);
    expect((await sharp((await project.read(output.paths[1]))!).metadata()).width).toBe(96);
    const again = await service.invoke({ operation: "compose", run });
    expect(again.paths[0]).not.toBe(output.paths[0]);
    expect(JSON.parse((await project.read(output.paths[0]))!.toString()).width).toBe(96);
});

it("unmixes a grey edge, preserves red material and clears small interior spill without holes", () => {
    const width = 40, height = 40, pixels = Buffer.alloc(width * height * 4);
    const put = (x: number, y: number, rgba: number[]) => pixels.set(rgba, (y * width + x) * 4);
    for (let y = 2; y < 38; y++) for (let x = 2; x < 38; x++) put(x, y, [50, 50, 50, 255]);
    // 50% grey foreground + 50% magenta key.
    put(2, 10, [153, 25, 153, 255]);
    put(2, 11, [200, 20, 20, 255]);
    put(20, 20, [153, 25, 153, 255]);
    // A sizeable intentional purple material must survive unchanged in the interior.
    for (let y = 10; y < 15; y++) for (let x = 10; x < 15; x++) put(x, y, [180, 40, 180, 255]);
    const result = despill(pixels, width, height, [255, 0, 255]);
    expect(result).toEqual({ edgePixels: 1, trappedPixels: 1 });
    const at = (x: number, y: number) => [...pixels.subarray((y * width + x) * 4, (y * width + x) * 4 + 4)];
    expect(at(2, 10)).toEqual([50, 50, 50, 127]);
    expect(at(2, 11)).toEqual([200, 20, 20, 255]);
    expect(at(20, 20)).toEqual([50, 50, 50, 255]);
    expect(at(12, 12)).toEqual([180, 40, 180, 255]);
});

it("corrects known horizontal drift without flattening vertical motion, and can be disabled", async () => {
    const width = 64, height = 64, count = 3, pixels = Buffer.alloc(width * height * count * 4);
    const shifts = [0, 4, -3], jumps = [0, 3, -2];
    for (let f = 0; f < count; f++) for (let y = 14; y < 44; y++) for (let x = 25; x < 37; x++) {
        pixels.set([100, 100, 100, 255], (((y + jumps[f]) * width * count) + f * width + x + shifts[f]) * 4);
    }
    const png = await sharp(pixels, { raw: { width: width * count, height, channels: 4 } }).png().toBuffer();
    const result = await extractRow(png, count, 64, "#ff00ff", 40);
    expect(result.registration.offsets.map(offset => offset.dx)).toEqual([0, -4, 3]);
    const before = await inspectFrames(result.beforeFrames, "#ff00ff", true);
    const after = await inspectFrames(result.frames, "#ff00ff", true);
    expect(after.horizontalDrift).toBeLessThanOrEqual(1);
    expect(before.horizontalDrift).toBeGreaterThan(7);
    expect(after.frames.map(frame => frame.bounds.top)).toEqual(before.frames.map(frame => frame.bounds.top));
    const disabled = await extractRow(png, count, 64, "#ff00ff", 40, { align: false, despill: false });
    expect(disabled.registration.offsets.every(offset => offset.dx === 0)).toBe(true);
    expect(disabled.frames).toEqual(disabled.beforeFrames);
});

it("flags visually static frames without claiming to certify gait", async () => {
    const frame = await sharp({ create: { width: 32, height: 32, channels: 4, background: "#888" } }).png().toBuffer();
    const quality = await inspectFrames([frame, frame], "#ff00ff", true);
    expect(quality.status).toBe("review");
    expect(quality.meanMotion).toBe(0);
    expect(quality.transitions.every(item => item.nearDuplicate && item.hashDistance === 0)).toBe(true);
    expect(quality.requiresVisualReview).toBe(true);
});

it("preserves motion references and sends them as a separate role without paid prepare calls", async () => {
    const { service, project, generate } = await fixture();
    const source = await row();
    await project.writeNew("assets/gait.png", source);
    const motionRun = "assets/with-motion";
    await service.invoke({ operation: "prepare", run: motionRun, request: {
        ...request, states: [{ ...request.states[0], motionReference: "assets/gait.png" }],
    } });
    expect(generate).not.toHaveBeenCalled();
    await project.write("assets/gait.png", "edited after preparation");
    expect(await project.read(`${motionRun}/motion/jump.png`)).toEqual(source);
    await service.invoke({ operation: "generate", run: motionRun, state: "jump" });
    expect(generate.mock.calls[0][0].references).toEqual([`${motionRun}/base.png`, `${motionRun}/guides/jump.png`, `${motionRun}/motion/jump.png`]);
    expect(generate.mock.calls[0][0].prompt).toContain("Reference 3 defines motion only");
    await expect(service.invoke({ operation: "prepare", run: "assets/bad-motion", request: {
        ...request, states: [{ ...request.states[0], motionReference: "assets/missing.png" }],
    } })).rejects.toThrow("Motion reference");
    expect(await project.exists("assets/bad-motion/base.png")).toBe(false);
});

it("removes key colour and preserves vertical motion with a shared crop", async () => {
    const result = await extractRow(await row(), 2, 32, "#ff00ff", 40);
    const topOf = async (image: Buffer) => {
        const { data, info } = await sharp(image).raw().toBuffer({ resolveWithObject: true });
        for (let p = 3; p < data.length; p += 4) if (data[p]) return Math.floor((p / 4) / info.width);
        return -1;
    };
    expect(await topOf(result.frames[0])).toBeGreaterThan(await topOf(result.frames[1]));
    expect((await sharp(result.frames[0]).raw().toBuffer())[3]).toBe(0);
    await expect(extractRow(await row(true), 2, 32, "#ff00ff", 40)).rejects.toThrow("empty");
    await expect(extractRow(await row(false, true), 2, 32, "#ff00ff", 40)).rejects.toThrow("boundary");
    await expect(extractRow(await row(), 3, 32, "#ff00ff", 40)).rejects.toThrow("divide");
});

it("blocks ambiguous paid retries across service instances and cancellation before work", async () => {
    const { service, project, generate } = await fixture();
    generate.mockRejectedValueOnce(new Error("uncertain provider outcome"));
    await expect(service.invoke({ operation: "generate", run, state: "jump" })).rejects.toThrow("not retried");
    const other = new SpriteAnimationService(project, () => generate);
    await expect(other.invoke({ operation: "generate", run, state: "jump" })).rejects.toThrow();
    expect(generate).toHaveBeenCalledOnce();
    await expect(service.invoke({ operation: "compose", run }, AbortSignal.abort())).rejects.toThrow();
    expect(await project.exists(`${run}/raw/jump.png`)).toBe(false);
});

it("can compose imported rows without contacting a model and rejects invalid selections", async () => {
    const { service, project, generate } = await fixture();
    const raw = await project.read(request.reference);
    await expect(service.invoke({ operation: "compose", run })).rejects.toThrow("Missing");
    await expect(service.invoke({ operation: "compose", run, selections: [{ state: "jump", source: request.reference, order: [4] }] })).rejects.toThrow("out of range");
    const result = await service.invoke({ operation: "compose", run, selections: [{ state: "jump", source: request.reference }] });
    expect(result.paths).toHaveLength(5);
    expect(generate).not.toHaveBeenCalled();
    expect(await project.read(request.reference)).toEqual(raw);
});

it("rejects path traversal and colliding state names before filesystem operations", () => {
    for (const bad of ["../run", "assets/../run", "assets/run:stream", "assets/run\\bad"]) {
        expect(spriteAnimationSchema.safeParse({ operation: "prepare", run: bad, request }).success).toBe(false);
    }
    expect(spriteAnimationSchema.safeParse({ operation: "prepare", run,
        request: { ...request, states: [request.states[0], { ...request.states[0], name: "JUMP" }] } }).success).toBe(false);
});
