import { createFauxCore, fauxAssistantMessage } from "@earendil-works/pi-ai";
import { describe, expect, it } from "vitest";
import { toProxyEvent } from "./proxy-events.js";

describe("toProxyEvent", () => {
    it("removes partial messages from streamed Pi events", async () => {
        const faux = createFauxCore({ provider: "faux-editor-host" });
        faux.setResponses([fauxAssistantMessage("hello from host")]);
        const events: unknown[] = [];

        for await (const event of faux.streamSimple(faux.getModel(), { messages: [] })) {
            events.push(toProxyEvent(event));
        }

        expect(events.at(0)).toEqual({ type: "start" });
        expect(events.at(-1)).toMatchObject({ type: "done", reason: "stop" });
        expect(JSON.stringify(events)).not.toContain("partial");
    });
});
