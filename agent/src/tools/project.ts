import type { AgentTool } from "@earendil-works/pi-agent-core";
import type { TSchema } from "typebox";
import { z } from "zod/v4";
import type { HostProjectService } from "@entisium/devkit/workspace/project-service";

export function createProjectTools(project: HostProjectService): AgentTool<any>[] {
    const definitions = [
        { name: "project_list", description: "List files in the current project.", schema: z.object({}).strict(), execute: () => project.list() },
        { name: "project_read", description: "Read a UTF-8 project file.", schema: z.object({ path: z.string().min(1) }).strict(), execute: async (input: any) => {
            const content = await project.read(input.path);
            if (!content) throw new Error(`File not found: ${input.path}`);
            if (content.length > 1024 * 1024) throw new Error("Text read exceeds 1 MiB.");
            return { path: input.path, content: content.toString("utf8") };
        } },
        { name: "project_write", description: "Save a UTF-8 project file. Changes are loaded on the next native runtime start.", schema: z.object({ path: z.string().min(1), content: z.string().max(1024 * 1024) }).strict(), execute: async (input: any) => {
            await project.write(input.path, input.content); return { path: input.path, saved: true };
        } },
    ];
    return definitions.map((tool) => ({
        name: tool.name, label: tool.name, description: tool.description,
        parameters: z.toJSONSchema(tool.schema) as TSchema,
        execute: async (_id, input, signal) => {
            signal?.throwIfAborted();
            const value = await tool.execute(tool.schema.parse(input));
            return { content: [{ type: "text" as const, text: JSON.stringify(value) }], details: value };
        },
    }));
}
