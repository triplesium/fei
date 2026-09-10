import type { ImageOptions } from "../../contracts/image-generation.js";

export const falGptImageModel = "openai/gpt-image-2.5/sunburst/text-to-image";

/** This endpoint's schema is model-specific, not a contract for every fal model. */
export function falImageOptions(options: ImageOptions, model: string) {
    if (model !== falGptImageModel) throw new Error("No image adapter is available for this fal model. Configure a supported image endpoint.");
    if (options.seed !== undefined) throw new Error("seed is not supported by the selected fal.ai image endpoint.");
    const { size, resolution, aspect_ratio, ...rest } = options;
    return { ...rest, image_size: falImageSize(size, resolution, aspect_ratio) };
}

function falImageSize(size: string | undefined, resolution: ImageOptions["resolution"], ratio: ImageOptions["aspect_ratio"]) {
    if (size) {
        const [width, height] = size.split("x").map(Number);
        validateFalDimensions(width, height);
        return { width, height };
    }
    if (ratio === "auto") {
        if (resolution !== "1K") throw new Error("fal.ai cannot combine an automatic aspect ratio with an explicit resolution tier.");
        return "auto";
    }
    const presets: Record<string, string> = {
        "1:1": "square_hd", "4:3": "landscape_4_3", "3:4": "portrait_4_3",
        "16:9": "landscape_16_9", "9:16": "portrait_16_9",
    };
    if (resolution === "512") {
        if (ratio !== "1:1") throw new Error("fal.ai only supports the 512 tier with a 1:1 aspect ratio.");
        return "square";
    }
    if (resolution === "1K" && presets[ratio!]) return presets[ratio!];

    const [x, y] = ratio!.split(":").map(Number);
    if (Math.max(x / y, y / x) > 3) throw new Error("fal.ai GPT Image 2.5 supports aspect ratios up to 3:1.");
    const maxEdge = resolution === "4K" ? 3840 : resolution === "2K" ? 1920 : 1024;
    let scale = maxEdge / Math.max(x, y);
    const minPixels = 655_360;
    const maxPixels = 8_294_400;
    let round = Math.floor;
    if (x * scale * y * scale < minPixels) {
        scale = Math.sqrt(minPixels / (x * y));
        round = Math.ceil;
    }
    if (x * scale * y * scale > maxPixels) scale = Math.sqrt(maxPixels / (x * y));
    let width = round(x * scale / 16) * 16;
    let height = round(y * scale / 16) * 16;
    while (width * height > maxPixels) {
        if (width >= height) width -= 16;
        else height -= 16;
    }
    validateFalDimensions(width, height);
    return { width, height };
}

function validateFalDimensions(width: number, height: number) {
    if (width % 16 || height % 16 || Math.max(width, height) > 3840 || Math.max(width / height, height / width) > 3 ||
        width * height < 655_360 || width * height > 8_294_400) {
        throw new Error("fal.ai GPT Image 2.5 dimensions must be multiples of 16, at most 3840px per edge and 655360–8294400 pixels with an aspect ratio up to 3:1.");
    }
}
