export type RuntimeState = "stopped" | "starting" | "running" | "failed";

export interface RuntimeProjectFile {
    path: string;
    content: string | Uint8Array<ArrayBuffer>;
}

export interface RuntimeSession {
    channelId: string;
    files: RuntimeProjectFile[];
    source: string;
}

export interface RuntimeSnapshot {
    state: RuntimeState;
    detail: string;
    script: string;
    frame: string;
    session: RuntimeSession | null;
}

export type RuntimeEvent =
    | { type: "snapshot"; snapshot: RuntimeSnapshot }
    | {
          type: "log";
          level: "info" | "error";
          source: "editor" | "game" | "runtime";
          message: string;
      };

export type RuntimeEventListener = (event: RuntimeEvent) => void;
