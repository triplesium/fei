import { cva, type VariantProps } from "class-variance-authority";
import { Check, ChevronDown } from "lucide-react";
import {
    createContext,
    useCallback,
    useContext,
    useEffect,
    useMemo,
    useRef,
    useState,
    type ComponentProps,
    type ReactNode,
} from "react";
import {
    Command,
    CommandEmpty,
    CommandGroup,
    CommandInput,
    CommandItem,
    CommandList,
    CommandSeparator,
} from "@/components/ui/command";
import { Popover, PopoverContent, PopoverTrigger } from "@/components/ui/popover";
import { cn } from "@/lib/utils";

export interface ModelOption {
    id: string;
    name: string;
    description?: string;
    icon?: ReactNode;
    disabled?: boolean;
    keywords?: readonly string[];
}

interface ModelSelectorContextValue {
    models: readonly ModelOption[];
    value?: string;
    selectedModel?: ModelOption;
    setValue(value: string): void;
    setOpen(open: boolean): void;
}

const ModelSelectorContext = createContext<ModelSelectorContextValue | null>(null);

function useModelSelectorContext(): ModelSelectorContextValue {
    const context = useContext(ModelSelectorContext);
    if (!context) throw new Error("ModelSelector components must be used within ModelSelectorRoot.");
    return context;
}

function useControllableState<T>({
    value,
    defaultValue,
    onChange,
}: {
    value: T | undefined;
    defaultValue: T;
    onChange?: (value: T) => void;
}): readonly [T, (value: T) => void] {
    const [internalValue, setInternalValue] = useState(defaultValue);
    const controlled = value !== undefined;
    const currentValue = controlled ? value : internalValue;
    const onChangeRef = useRef(onChange);
    useEffect(() => {
        onChangeRef.current = onChange;
    });
    const setValue = useCallback(
        (nextValue: T) => {
            if (!controlled) setInternalValue(nextValue);
            onChangeRef.current?.(nextValue);
        },
        [controlled],
    );
    return [currentValue, setValue] as const;
}

export interface ModelSelectorRootProps {
    models: readonly ModelOption[];
    value?: string;
    defaultValue?: string;
    onValueChange?: (value: string) => void;
    open?: boolean;
    defaultOpen?: boolean;
    onOpenChange?: (open: boolean) => void;
    children: ReactNode;
}

export function ModelSelectorRoot({
    models,
    value: valueProp,
    defaultValue,
    onValueChange,
    open: openProp,
    defaultOpen = false,
    onOpenChange,
    children,
}: ModelSelectorRootProps) {
    const [value, setValue] = useControllableState({
        value: valueProp,
        defaultValue: defaultValue ?? models[0]?.id ?? "",
        onChange: onValueChange,
    });
    const [open, setOpen] = useControllableState({
        value: openProp,
        defaultValue: defaultOpen,
        onChange: onOpenChange,
    });
    const selectedModel = models.find((model) => model.id === value);
    const contextValue = useMemo(
        () => ({ models, value, selectedModel, setValue, setOpen }),
        [models, selectedModel, setOpen, setValue, value],
    );

    return (
        <ModelSelectorContext.Provider value={contextValue}>
            <Popover open={open} onOpenChange={setOpen}>
                {children}
            </Popover>
        </ModelSelectorContext.Provider>
    );
}

const modelSelectorTriggerVariants = cva(
    "m-0 flex w-fit appearance-none items-center justify-between gap-2 overflow-hidden rounded-md border-0 bg-transparent text-sm whitespace-nowrap outline-none transition-colors focus-visible:ring-1 focus-visible:ring-ring/50 disabled:cursor-not-allowed disabled:opacity-50 [&_svg]:pointer-events-none [&_svg]:shrink-0",
    {
        variants: {
            variant: {
                outline: "border border-input bg-transparent hover:bg-accent hover:text-accent-foreground",
                ghost: "hover:bg-accent hover:text-accent-foreground",
                muted: "bg-secondary text-secondary-foreground hover:bg-secondary/80",
            },
            size: {
                default: "h-9 px-3 py-2",
                sm: "h-8 px-2.5 py-1.5 text-xs",
                lg: "h-10 px-4 py-2.5",
            },
        },
        defaultVariants: { variant: "outline", size: "default" },
    },
);

export interface ModelSelectorTriggerProps
    extends ComponentProps<"button">,
        VariantProps<typeof modelSelectorTriggerVariants> {}

export function ModelSelectorTrigger({
    className,
    variant,
    size,
    children,
    onKeyDown,
    ...props
}: ModelSelectorTriggerProps) {
    const { setOpen } = useModelSelectorContext();
    return (
        <PopoverTrigger asChild>
            <button
                type="button"
                data-slot="model-selector-trigger"
                role="combobox"
                aria-haspopup="listbox"
                className={cn(modelSelectorTriggerVariants({ variant, size }), className)}
                onKeyDown={(event) => {
                    onKeyDown?.(event);
                    if (!event.defaultPrevented && (event.key === "ArrowDown" || event.key === "ArrowUp")) {
                        event.preventDefault();
                        setOpen(true);
                    }
                }}
                {...props}
            >
                {children ?? <ModelSelectorValue />}
                <ChevronDown className="size-3.5 text-muted-foreground" />
            </button>
        </PopoverTrigger>
    );
}

export function ModelSelectorValue({
    className,
    placeholder = "Select model",
    ...props
}: ComponentProps<"span"> & { placeholder?: string }) {
    const { selectedModel } = useModelSelectorContext();
    return (
        <span className={cn("flex min-w-0 items-center gap-2", className)} {...props}>
            {selectedModel?.icon}
            <span className="truncate">{selectedModel?.name ?? placeholder}</span>
        </span>
    );
}

export function ModelSelectorContent({
    className,
    children,
    side = "top",
    align = "start",
    sideOffset = 8,
    ...props
}: ComponentProps<typeof PopoverContent>) {
    return (
        <PopoverContent
            data-slot="model-selector-content"
            side={side}
            align={align}
            sideOffset={sideOffset}
            className={cn("w-[320px] overflow-hidden rounded-xl p-0", className)}
            {...props}
        >
            <Command loop>{children}</Command>
        </PopoverContent>
    );
}

export function ModelSelectorSearch(props: ComponentProps<typeof CommandInput>) {
    return <CommandInput data-slot="model-selector-search" {...props} />;
}

export function ModelSelectorList(props: ComponentProps<typeof CommandList>) {
    return <CommandList data-slot="model-selector-list" {...props} />;
}

export function ModelSelectorEmpty({ children = "No models found.", ...props }: ComponentProps<typeof CommandEmpty>) {
    return <CommandEmpty data-slot="model-selector-empty" {...props}>{children}</CommandEmpty>;
}

export function ModelSelectorGroup(props: ComponentProps<typeof CommandGroup>) {
    return <CommandGroup data-slot="model-selector-group" {...props} />;
}

export function ModelSelectorSeparator(props: ComponentProps<typeof CommandSeparator>) {
    return <CommandSeparator data-slot="model-selector-separator" {...props} />;
}

export function ModelSelectorItem({ model, className }: { model: ModelOption; className?: string }) {
    const { value, setValue, setOpen } = useModelSelectorContext();
    const selected = model.id === value;
    const searchValue = [model.name, model.id, ...(model.keywords ?? [])].join(" ");
    return (
        <CommandItem
            data-slot="model-selector-item"
            value={searchValue}
            disabled={model.disabled}
            className={cn("min-h-11", className)}
            onSelect={() => {
                setValue(model.id);
                setOpen(false);
            }}
        >
            <span className="grid size-4 shrink-0 place-items-center">{model.icon}</span>
            <span className="min-w-0 flex-1">
                <span className="block truncate font-medium text-foreground">{model.name}</span>
                {model.description && (
                    <span className="mt-0.5 block truncate text-xs text-muted-foreground">{model.description}</span>
                )}
            </span>
            <Check className={cn("size-3.5 text-primary", !selected && "opacity-0")} />
        </CommandItem>
    );
}
