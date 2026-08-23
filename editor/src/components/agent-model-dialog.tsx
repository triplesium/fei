import { ArrowLeft, Check, KeyRound, Pencil, Plus, Settings2, Trash2 } from "lucide-react";
import type { KeyboardEvent, ReactNode } from "react";
import type {
    EditorModelProviderSummary,
    OpenAICompatibleApi,
} from "@/services/editor-host-client";
import { Button } from "@/components/ui/button";
import { Checkbox } from "@/components/ui/checkbox";
import { Input } from "@/components/ui/input";
import { NativeSelect } from "@/components/ui/native-select";
import { cn } from "@/lib/utils";

export interface AgentModelDraft {
    providerId: string;
    modelId: string;
    providerName: string;
    baseUrl: string;
    api: OpenAICompatibleApi;
    modelName: string;
    reasoning: boolean;
    contextWindow: string;
    maxTokens: string;
    apiKey: string;
}

export type AgentModelEditorTarget =
    | { kind: "provider"; providerId: string; newProvider: boolean }
    | { kind: "model"; providerId: string; modelId?: string };

export interface AgentModelSettingsProps {
    draft: AgentModelDraft;
    target: AgentModelEditorTarget | null;
    providers?: EditorModelProviderSummary[];
    activeProviderId?: string;
    activeModelId?: string;
    credentialConfigured: boolean;
    providerValid: boolean;
    modelValid: boolean;
    saving: boolean;
    error?: string;
    onDraftChange(patch: Partial<AgentModelDraft>): void;
    onActivate(providerId: string, modelId: string): void | Promise<void>;
    onAddProvider(): void;
    onAddModel(providerId: string): void;
    onEditProvider(providerId: string): void;
    onEditModel(providerId: string, modelId: string): void;
    onBack(): void;
    onSaveProvider(): void | Promise<void>;
    onSaveModel(): void | Promise<void>;
    onDeleteProvider(providerId: string): void | Promise<void>;
    onDeleteModel(providerId: string, modelId: string): void | Promise<void>;
    onRemoveCredential(): void | Promise<void>;
}

function Field({ label, hint, className, children }: {
    label: string;
    hint?: string;
    className?: string;
    children: ReactNode;
}) {
    return (
        <label className={cn("grid gap-1.5 text-[10px] font-semibold text-[#b7bdc8]", className)}>
            <span>
                {label}
                {hint && <small className="ml-1.5 text-[10px] font-normal text-muted-foreground">{hint}</small>}
            </span>
            {children}
        </label>
    );
}

function apiLabel(api: OpenAICompatibleApi): string {
    return api === "responses" ? "Responses API" : "Chat Completions";
}

function ProviderStatus({ configured }: { configured: boolean }) {
    if (configured) return null;
    return (
        <span className="inline-flex items-center gap-1.5 text-[10px] text-amber-300">
            <span className="size-1.5 rounded-full bg-amber-300" />
            API key required
        </span>
    );
}

function EditorHeader({ title, description, onBack }: { title: string; description: string; onBack(): void }) {
    return (
        <div className="flex items-start gap-3 border-b border-border/70 pb-4">
            <Button variant="ghost" size="icon" aria-label="Back to models" onClick={onBack}>
                <ArrowLeft className="size-4" />
            </Button>
            <div>
                <h3 className="m-0 text-sm font-semibold text-foreground">{title}</h3>
                <p className="mb-0 mt-1 text-[10px] leading-4 text-muted-foreground">{description}</p>
            </div>
        </div>
    );
}

export function AgentModelSettings({
    draft,
    target,
    providers,
    activeProviderId,
    activeModelId,
    credentialConfigured,
    providerValid,
    modelValid,
    saving,
    error,
    onDraftChange,
    onActivate,
    onAddProvider,
    onAddModel,
    onEditProvider,
    onEditModel,
    onBack,
    onSaveProvider,
    onSaveModel,
    onDeleteProvider,
    onDeleteModel,
    onRemoveCredential,
}: AgentModelSettingsProps) {
    if (target?.kind === "provider") {
        const saveDisabled = saving || !providerValid || (!credentialConfigured && !draft.apiKey.trim());
        const submitOnEnter = (event: KeyboardEvent<HTMLInputElement>): void => {
            if (event.key === "Enter" && !saveDisabled) void onSaveProvider();
        };
        return (
            <section className="grid gap-4" aria-label="Provider settings">
                <EditorHeader
                    title={target.newProvider ? "Add provider" : `Provider · ${draft.providerName}`}
                    description="Configure the OpenAI-compatible endpoint and the credential shared by its models."
                    onBack={onBack}
                />
                <div className="grid grid-cols-2 gap-3 max-[560px]:grid-cols-1">
                    <Field label="Provider ID" hint="cannot be changed later">
                        <Input
                            value={draft.providerId}
                            disabled={!target.newProvider}
                            spellCheck={false}
                            onChange={(event) => onDraftChange({ providerId: event.target.value })}
                        />
                    </Field>
                    <Field label="Provider name">
                        <Input value={draft.providerName} onChange={(event) => onDraftChange({ providerName: event.target.value })} />
                    </Field>
                </div>
                <div className="grid grid-cols-[minmax(0,1fr)_180px] gap-3 max-[560px]:grid-cols-1">
                    <Field label="API Base URL" hint="include the API version path when required">
                        <Input value={draft.baseUrl} spellCheck={false} placeholder="https://api.example.com/v1" onChange={(event) => onDraftChange({ baseUrl: event.target.value })} />
                    </Field>
                    <Field label="API format">
                        <NativeSelect value={draft.api} onChange={(event) => onDraftChange({ api: event.target.value as OpenAICompatibleApi })}>
                            <option value="responses">Responses API</option>
                            <option value="chat-completions">Chat Completions</option>
                        </NativeSelect>
                    </Field>
                </div>
                <Field label="API key">
                    <Input
                        type="password"
                        value={draft.apiKey}
                        autoComplete="off"
                        spellCheck={false}
                        placeholder={credentialConfigured ? "Leave blank to keep the saved key" : "Enter an API key"}
                        onChange={(event) => onDraftChange({ apiKey: event.target.value })}
                        onKeyDown={submitOnEnter}
                    />
                </Field>
                <p className="m-0 flex items-center gap-1.5 text-[10px] leading-4 text-muted-foreground">
                    <KeyRound className="size-3" /> Saved keys are never returned to the browser.
                </p>
                {error && <p className="m-0 rounded-md border border-destructive/30 bg-destructive/10 px-3 py-2 text-[10px] leading-4 text-[#ff9aaa]">{error}</p>}
                <div className="flex flex-wrap justify-end gap-2 border-t border-border/70 pt-4">
                    {!target.newProvider && (
                        <Button variant="destructive" disabled={saving || !credentialConfigured} onClick={() => void onRemoveCredential()}>
                            Remove key
                        </Button>
                    )}
                    <Button variant="outline" disabled={saving} onClick={onBack}>Cancel</Button>
                    <Button disabled={saveDisabled} onClick={() => void onSaveProvider()}>{saving ? "Saving…" : "Save provider"}</Button>
                </div>
            </section>
        );
    }

    if (target?.kind === "model") {
        const provider = providers?.find((candidate) => candidate.id === target.providerId);
        return (
            <section className="grid gap-4" aria-label="Model settings">
                <EditorHeader
                    title={target.modelId ? `Edit model · ${draft.modelName}` : `Add model to ${provider?.name ?? target.providerId}`}
                    description="Model settings describe capabilities and limits; endpoint and credentials remain provider-level."
                    onBack={onBack}
                />
                <div className="rounded-lg border border-border/70 bg-black/10 px-3 py-2.5">
                    <div className="flex flex-wrap items-center gap-2 text-[10px]">
                        <span className="font-semibold text-foreground">{provider?.name ?? target.providerId}</span>
                        {provider && <ProviderStatus configured={provider.configured} />}
                    </div>
                    {provider && <p className="mb-0 mt-1 truncate text-[10px] text-muted-foreground">{apiLabel(provider.api)} · {provider.baseUrl}</p>}
                </div>
                <div className="grid grid-cols-2 gap-3 max-[560px]:grid-cols-1">
                    <Field label="Model ID">
                        <Input value={draft.modelId} spellCheck={false} onChange={(event) => onDraftChange({ modelId: event.target.value })} />
                    </Field>
                    <Field label="Display name">
                        <Input value={draft.modelName} onChange={(event) => onDraftChange({ modelName: event.target.value })} />
                    </Field>
                </div>
                <div className="rounded-lg border border-border/80 bg-white/[0.02] p-3">
                    <label className="flex items-center gap-2 text-[10px] text-[#b7bdc8]">
                        <Checkbox checked={draft.reasoning} onCheckedChange={(checked) => onDraftChange({ reasoning: checked === true })} />
                        <span>Model emits reasoning output</span>
                    </label>
                    <div className="mt-3 grid grid-cols-2 gap-3 max-[560px]:grid-cols-1">
                        <Field label="Context window">
                            <Input type="number" min="1024" value={draft.contextWindow} onChange={(event) => onDraftChange({ contextWindow: event.target.value })} />
                        </Field>
                        <Field label="Maximum output">
                            <Input type="number" min="256" value={draft.maxTokens} onChange={(event) => onDraftChange({ maxTokens: event.target.value })} />
                        </Field>
                    </div>
                </div>
                {error && <p className="m-0 rounded-md border border-destructive/30 bg-destructive/10 px-3 py-2 text-[10px] leading-4 text-[#ff9aaa]">{error}</p>}
                <div className="flex justify-end gap-2 border-t border-border/70 pt-4">
                    <Button variant="outline" disabled={saving} onClick={onBack}>Cancel</Button>
                    <Button disabled={saving || !modelValid} onClick={() => void onSaveModel()}>{saving ? "Saving…" : "Save model"}</Button>
                </div>
            </section>
        );
    }

    return (
        <section className="grid gap-2" aria-label="Configured models">
            <div className="flex items-center justify-between gap-3">
                <div>
                    <h3 className="m-0 text-[11px] font-semibold text-foreground">Models</h3>
                    <p className="mb-0 mt-1 text-[10px] text-muted-foreground">Every provider uses an OpenAI-compatible transport.</p>
                </div>
                <Button variant="outline" size="sm" onClick={onAddProvider}>
                    <Plus className="size-3.5" /> Add provider
                </Button>
            </div>
            <div className="grid gap-2">
                {providers?.map((provider) => (
                    <div key={provider.id} className="overflow-hidden rounded-lg border border-border/80 bg-black/10">
                        <div className="flex items-start justify-between gap-3 border-b border-border/60 px-3 py-2.5">
                            <div className="min-w-0">
                                <div className="flex flex-wrap items-center gap-2">
                                    <span className="text-[11px] font-semibold text-foreground">{provider.name}</span>
                                    <ProviderStatus configured={provider.configured} />
                                </div>
                                <p className="mb-0 mt-1 truncate text-[10px] text-muted-foreground" title={provider.baseUrl}>{apiLabel(provider.api)} · {provider.baseUrl}</p>
                            </div>
                            <div className="flex items-center gap-1">
                                <Button variant="ghost" size="sm" onClick={() => onAddModel(provider.id)}><Plus className="size-3" /> Model</Button>
                                <Button variant="ghost" size="icon" aria-label={`Configure ${provider.name}`} onClick={() => onEditProvider(provider.id)}><Settings2 className="size-3.5" /></Button>
                                <Button
                                    variant="ghost"
                                    size="icon"
                                    className="hover:text-destructive"
                                    aria-label={`Delete provider ${provider.name}`}
                                    onClick={() => {
                                        if (window.confirm(`Delete provider ${provider.name}, its models, and its saved credential?`)) void onDeleteProvider(provider.id);
                                    }}
                                ><Trash2 className="size-3.5" /></Button>
                            </div>
                        </div>
                        {provider.models.length > 0 ? (
                            <div className="divide-y divide-border/50">
                                {provider.models.map((model) => {
                                    const active = provider.id === activeProviderId && model.id === activeModelId;
                                    return (
                                        <div key={model.id} className={cn("flex items-center gap-3 px-3 py-2.5", active && "bg-primary/[0.055]")}>
                                            <button type="button" className="m-0 flex min-w-0 flex-1 appearance-none items-center gap-3 border-0 bg-transparent p-0 text-left outline-none focus-visible:ring-2 focus-visible:ring-ring/35" onClick={() => void onActivate(provider.id, model.id)}>
                                                <span className={cn("grid size-5 shrink-0 place-items-center rounded-full border", active ? "border-primary bg-primary text-primary-foreground" : "border-border text-transparent")}><Check className="size-3" /></span>
                                                <span className="min-w-0 flex-1">
                                                    <span className="block truncate text-[11px] font-medium text-foreground">{model.name}</span>
                                                    <span className="mt-0.5 block truncate font-mono text-[10px] text-muted-foreground">{model.id}</span>
                                                </span>
                                            </button>
                                            <Button variant="ghost" size="icon" aria-label={`Edit ${model.name}`} onClick={() => onEditModel(provider.id, model.id)}><Pencil className="size-3.5" /></Button>
                                            <Button variant="ghost" size="icon" className="hover:text-destructive" aria-label={`Delete ${model.name}`} onClick={() => { if (window.confirm(`Delete ${model.name}?`)) void onDeleteModel(provider.id, model.id); }}><Trash2 className="size-3.5" /></Button>
                                        </div>
                                    );
                                })}
                            </div>
                        ) : (
                            <div className="flex items-center justify-between gap-3 px-3 py-3 text-[10px] text-muted-foreground">
                                <span>No models configured.</span>
                                <Button variant="ghost" size="sm" onClick={() => onAddModel(provider.id)}>Add first model</Button>
                            </div>
                        )}
                    </div>
                ))}
            </div>
            {error && <p className="m-0 rounded-md border border-destructive/30 bg-destructive/10 px-3 py-2 text-[10px] leading-4 text-[#ff9aaa]">{error}</p>}
        </section>
    );
}
