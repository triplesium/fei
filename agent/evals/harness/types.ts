import type { AgentTool } from "@earendil-works/pi-agent-core";

export interface Call {
    turn?: number;
    name: string;
    input: any;
    value?: any;
    error?: string;
}
export interface Evidence {
    files: Record<string, string>;
    calls: Call[];
    answer: string;
    state: Record<string, unknown>;
    verification?: Check[];
}
export interface Check { name: string; passed: boolean; detail?: string; dimension?: "outcome" | "verification" | "constraints" }
export type Invoke = (name: string, input: Record<string, unknown>) => Promise<any>;
export interface EvalCase {
    id: string;
    version?: number;
    tags?: string[];
    steps?: { id: string; instruction: string }[];
    environment: { runtime: "simulated" | "native"; fixture?: string };
    prompt: string;
    files: Record<string, string>;
    writable: boolean;
    fault?: "timeout" | "start" | "write";
    followUps?: string[];
    runtimeInterface?: string;
    prepare?(root: string): Promise<void>;
    verify?(root: string, output: string, signal: AbortSignal): Promise<Check[]>;
    grade(evidence: Evidence): Check[];
    reference(invoke: Invoke, turn?: number): Promise<string>;
}
export interface TrialResult {
    caseId: string;
    trial: number;
    mode: "agent" | "reference";
    status: "passed" | "failed" | "budget_exceeded" | "model_error" | "environment_error" | "grader_error";
    checks: Check[];
    error?: string;
    elapsedMs: number;
    toolCalls: number;
    tokens: number | null;
    stopReasons?: string[];
    directory: string;
}
export type Tools = AgentTool<any>[];
