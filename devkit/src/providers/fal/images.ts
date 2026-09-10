import { runFalQueue, type FalQueueOptions } from "./queue.js";

export async function generateFalImage(options: FalQueueOptions & { prompt: string; requestOptions: object }) {
    try {
        return await runFalQueue(options, {
            prompt: options.prompt, ...options.requestOptions, num_images: 1, output_format: "png", sync_mode: true,
        }, (data) => {
            const body = data as { images?: { url?: unknown; content_type?: unknown }[] };
            const file = body?.images?.[0];
            const match = typeof file?.url === "string" ? /^data:image\/png;base64,([A-Za-z0-9+/]*={0,2})$/.exec(file.url) : undefined;
            if (!match) throw new Error("Invalid fal image response.");
            return { encoded: match[1], mediaType: file?.content_type };
        });
    } catch {
        options.signal.throwIfAborted();
        throw new Error("Image generation through fal.ai failed. Check API access, quota and model configuration.");
    }
}
