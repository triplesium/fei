import { createFalClient, type FalClient } from "@fal-ai/client";

export interface FalQueueOptions {
    model: string;
    apiKey: string;
    signal: AbortSignal;
    fetch?: typeof fetch;
    createFalClient?: (credentials: string) => Pick<FalClient, "queue">;
}

/** Shared fal transport; input/output schemas belong to the consuming model adapter. */
export async function runFalQueue<T>(options: FalQueueOptions, input: Record<string, unknown>, decode: (data: unknown) => T): Promise<T> {
    const client = options.createFalClient?.(options.apiKey) ?? createFalClient({
        credentials: options.apiKey, fetch: options.fetch, retry: { maxRetries: 0 },
    });
    let requestId: string | undefined;
    try {
        const submitted = await client.queue.submit(options.model, { input, abortSignal: options.signal });
        requestId = submitted.request_id;
        await client.queue.subscribeToStatus(options.model, { requestId, abortSignal: options.signal, mode: "polling" });
        return decode((await client.queue.result(options.model, { requestId, abortSignal: options.signal })).data);
    } catch {
        if (requestId) await client.queue.cancel(options.model, { requestId }).catch(() => undefined);
        options.signal.throwIfAborted();
        throw new Error("The fal.ai queue request failed. Check API access, quota and model configuration.");
    }
}
