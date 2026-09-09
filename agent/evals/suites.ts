import type { EvalCase } from "./harness/types.js";

/** Suites select tasks only. Membership never determines their environment or tools. */
export const suites: Record<string, readonly string[]> = {
    project: ["double-speed", "disable-sound", "change-lives", "repair-json", "read-only-speed", "create-config-summary"],
    behavior: ["uncertain-action", "startup-failure", "write-failure", "stop-owned-runtime"],
    native: ["native-observe", "native-set-rotation"],
    capability: ["repair-luau-startup", "repair-interaction-crash", "verify-double-movement", "repair-air-jump",
        "repair-premature-victory", "preserve-change-across-turns", "fixed-course-win", "reactive-gate-crossing"],
    framework: ["repair-air-jump", "preserve-change-across-turns", "fixed-course-win"],
};

export function selectCases(cases: EvalCase[], options: { suite?: string; case?: string; list?: boolean }): EvalCase[] {
    if (options.suite && options.suite !== "all" && !Object.hasOwn(suites, options.suite)) throw new Error(`Unknown suite: ${options.suite}`);
    const ids = new Set(cases.map(task => task.id));
    if (ids.size !== cases.length) throw new Error("Duplicate eval task ID.");
    for (const [suite, members] of Object.entries(suites)) {
        if (new Set(members).size !== members.length || members.some(id => !ids.has(id))) throw new Error(`Invalid suite membership: ${suite}`);
    }
    const selected = options.suite && options.suite !== "all" ? suites[options.suite]
        : options.suite === "all" || options.case || options.list ? undefined : [...suites.project, ...suites.behavior];
    return cases.filter(task => (!options.case || task.id === options.case) && (!selected || selected.includes(task.id)));
}
