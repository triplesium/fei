import { Settings2 } from "lucide-react";
import { useMemo, useRef, useState, type ReactNode } from "react";
import type { EditorModelProviderSummary } from "@/services/editor-host-client";
import { CommandItem } from "@/components/ui/command";
import {
    ModelSelectorContent,
    ModelSelectorEmpty,
    ModelSelectorGroup,
    ModelSelectorItem,
    ModelSelectorList,
    ModelSelectorRoot,
    ModelSelectorSearch,
    ModelSelectorSeparator,
    ModelSelectorTrigger,
    ModelSelectorValue,
    type ModelOption,
} from "@/components/assistant-ui/model-selector";

interface AgentModelSelectorProps {
    providers?: EditorModelProviderSummary[];
    activeProviderId?: string;
    activeModelId?: string;
    label: string;
    ready: boolean;
    disabled?: boolean;
    onSelectModel: (providerId: string, modelId: string) => void | Promise<void>;
    onManageModels: () => void;
}

function modelKey(providerId: string, modelId: string): string {
    return JSON.stringify([providerId, modelId]);
}

function providerStatus(configured: boolean): ReactNode {
    return (
        <span
            aria-hidden="true"
            className={configured
                ? "size-1.5 rounded-full bg-[#5ad49a] shadow-[0_0_0_3px_rgb(90_212_154/0.1)]"
                : "size-1.5 rounded-full bg-[#667085] shadow-[0_0_0_3px_rgb(102_112_133/0.1)]"}
        />
    );
}

export function AgentModelSelector({
    providers,
    activeProviderId,
    activeModelId,
    label,
    ready,
    disabled = false,
    onSelectModel,
    onManageModels,
}: AgentModelSelectorProps) {
    const [open, setOpen] = useState(false);
    const triggerRef = useRef<HTMLButtonElement>(null);
    const openedWithPointerRef = useRef(false);
    const models = useMemo<ModelOption[]>(
        () =>
            providers?.flatMap((provider) =>
                provider.models.map((model) => ({
                    id: modelKey(provider.id, model.id),
                    name: model.name,
                    icon: providerStatus(
                        provider.id === activeProviderId && model.id === activeModelId
                            ? ready
                            : provider.configured,
                    ),
                    keywords: [provider.id, provider.name, model.id],
                })),
            ) ?? [],
        [activeModelId, activeProviderId, providers, ready],
    );
    const modelsById = useMemo(() => new Map(models.map((model) => [model.id, model])), [models]);
    const activeValue = activeProviderId && activeModelId
        ? modelKey(activeProviderId, activeModelId)
        : undefined;

    const selectModel = (value: string): void => {
        const model = modelsById.get(value);
        if (!model) return;
        const parsed = JSON.parse(model.id) as [string, string];
        void onSelectModel(parsed[0], parsed[1]);
    };

    const manageModels = (): void => {
        setOpen(false);
        onManageModels();
    };

    const setSelectorOpen = (nextOpen: boolean): void => {
        setOpen(nextOpen);
        if (!nextOpen && openedWithPointerRef.current) {
            window.requestAnimationFrame(() => triggerRef.current?.blur());
        }
    };

    return (
        <ModelSelectorRoot
            models={models}
            value={activeValue}
            onValueChange={selectModel}
            open={open}
            onOpenChange={setSelectorOpen}
        >
            <ModelSelectorTrigger
                ref={triggerRef}
                variant="ghost"
                size="sm"
                disabled={disabled}
                aria-label="Select agent model"
                className="h-7 max-w-[220px] min-w-0 gap-1.5 rounded-full px-2 text-[11px]"
                onPointerDown={() => {
                    openedWithPointerRef.current = true;
                }}
                onKeyDown={() => {
                    openedWithPointerRef.current = false;
                }}
            >
                <ModelSelectorValue placeholder={label} />
            </ModelSelectorTrigger>
            <ModelSelectorContent className="w-[280px] [&_[cmdk-input-wrapper]]:h-8">
                <ModelSelectorSearch placeholder="Search models…" />
                <ModelSelectorList className="p-0.5">
                    <ModelSelectorEmpty />
                    {providers?.map((provider) => (
                        <ModelSelectorGroup
                            key={provider.id}
                            className="p-0.5 [&_[cmdk-group-heading]]:py-1"
                            heading={
                                <span className="flex items-center justify-between gap-3">
                                    <span>{provider.name}</span>
                                    {!provider.configured && (
                                        <span className="normal-case tracking-normal text-[#c79265]">
                                            Key required
                                        </span>
                                    )}
                                </span>
                            }
                        >
                            {provider.models.map((model) => (
                                <ModelSelectorItem
                                    key={`${provider.id}:${model.id}`}
                                    model={modelsById.get(modelKey(provider.id, model.id))!}
                                    className="min-h-8 py-1"
                                />
                            ))}
                        </ModelSelectorGroup>
                    ))}
                    <ModelSelectorSeparator className="my-0.5" />
                    <ModelSelectorGroup className="p-0.5">
                        <CommandItem className="min-h-8 py-1" value="manage configure models settings" onSelect={manageModels}>
                            <Settings2 className="size-3.5 text-muted-foreground" />
                            <span className="font-medium">Manage models…</span>
                        </CommandItem>
                    </ModelSelectorGroup>
                </ModelSelectorList>
            </ModelSelectorContent>
        </ModelSelectorRoot>
    );
}
