# DevKit and Agent configuration

DevKit services and Agent use one `config.yaml`. API keys are stored directly as
`providers.<id>.apiKey`. See [config.example.yaml](../config.example.yaml).

## Location and loading

- Agent CLI: `npm run agent -- --config <path> --project <project.yaml> --prompt <task>`.
- Editor host and Runtime MCP: set `ETS_CONFIG_PATH` before starting the process.
- Project configuration: `.entisium/config.yaml` inside the selected project directory.
- User default on Windows: `%APPDATA%/Entisium/config.yaml`.
- Default elsewhere: `$XDG_CONFIG_HOME/entisium/config.yaml`, or `~/.config/entisium/config.yaml`.

CLI `--config` takes priority over `ETS_CONFIG_PATH`, which takes priority over the
project configuration, then the user default path. Only one file is loaded; sections are not merged. A missing explicitly selected file or malformed YAML is an error.
When neither project nor user YAML exists, the Agent and Editor retain their previous
JSON model settings and encrypted credential store. Once YAML exists, they use its
providers and keys exclusively; old credentials are not merged or migrated.

CLI uses the directory containing `--project` project.yaml. Editor uses its startup `--project` directory or `ETS_EDITOR_PROJECT_DIR`; without one it uses the working directory. Runtime MCP uses its startup working directory. Discovery does not walk parent directories. Selection happens at startup; opening another project in the Editor does not switch configuration. Restart the host with that project selected to load its config.

Restart the host after manually changing model, image or runtime settings. API keys
are read when needed. Existing Editor model-setting operations write back to the
same YAML, preserving DevKit sections. Editor appearance remains in its existing
settings file; `editor` in YAML is reserved and preserved but not applied yet.

## Sections

`providers` maps connection IDs to optional `name`, `type`, `apiKey`,
`chat` and `images` settings. A connection can serve multiple capabilities
with one saved credential. `type` is `openai`, `openrouter`, `fal` or
`openai-compatible`. IDs matching the first three infer that type; other IDs
require an explicit type for platform defaults, or explicit capability connections.

- OpenAI defaults to Responses for chat and OpenAI Images for images, with
  `https://api.openai.com/v1` as each capability's base URL.
- OpenRouter defaults to Chat Completions and its dedicated Images API, with
  `https://openrouter.ai/api/v1` as each capability's base URL.
- fal defaults to its official image queue adapter. It does not create a chat
  connection; native fal LLM and video adapters are not implemented yet.
- Custom connections specify `chat: { api, baseUrl }` and/or
  `images: { api, baseUrl }`. Each capability has its own URL; overriding a chat
  URL never redirects images. Chat API formats are `responses` (default) and
  `chat-completions`; image adapters are `openai-images` (default),
  `openrouter-images` and `fal-images`. The fal image adapter rejects a custom
  image URL. Include the version path in URLs where needed.

Both `agent.model` and `imageGeneration.model` use `{ provider, id }`.
Model IDs are used directly, without prior registration. `providers.<id>.models`
is an optional chat catalogue and metadata override, not a whitelist. Each entry
requires only `id`; `contextWindow`, `maxTokens`, `name` and `reasoning` are optional
field overrides. Metadata merges each field from explicit overrides, platform
responses/cache, then bundled OpenAI/OpenRouter data or DeepSeek presets. Missing
fields remain unknown in metadata; an explicit `reasoning: false` overrides true.
Unknown models use operating defaults of 32,768 context tokens, 4,096 output
tokens, and reasoning disabled; these are not claims about upstream limits.
Add an override when a model needs different limits or reasoning support.
Merely selecting a model never writes a catalogue entry. Platform metadata is
cached separately from YAML. See [model-metadata.md](model-metadata.md) for
platform differences, refresh behavior, provenance and cache locations.

`agent.reasoning` accepts `off`, `minimal`, `low`, `medium`, `high`,
`xhigh`, or `max` (default `low`), and is disabled for models without
reasoning support. CLI `--provider` and `--model` override the saved selection,
including IDs absent from the catalogue. The Editor model selector accepts a
model ID in its search field and offers it under each chat provider. Existing
model-management forms edit optional metadata. Opening the selector loads the
platform catalogue asynchronously; **Refresh models** forces a refresh. `ETS_EDITOR_MODEL` also accepts
unlisted IDs on the active provider (or the first chat provider when none is active).

`imageGeneration` selects its model independently of the conversation model.
The chosen provider resolves the image adapter. OpenAI Images appends
`/images/generations`; OpenRouter appends `/images`; both require PNG data in
`data[0].b64_json`. fal uses its official queue. Its current model adapter is
specific to `openai/gpt-image-2.5/sunburst/text-to-image`; unsupported fal image
models are rejected before submission instead of receiving that model's schema.
`defaults` uses `resolution`, `aspect_ratio`, optional `size`, `quality`,
`background`, and `seed`; see [image-generation.md](image-generation.md).
`timeoutMs` defaults to 600000 and accepts 1000–1800000.
`ETS_IMAGE_MODEL` overrides the selected image ID; `ENTISIUM_IMAGE_MODEL`
remains a lower-priority alias. Image credentials prefer `OPENAI_API_KEY`,
`OPENROUTER_API_KEY`, or `FAL_KEY` according to platform type, then the selected
connection's stored key. Custom compatible connections use their own stored key.

Chat settings updates preserve image-only providers and other capability settings.
Deleting a chat provider still used by image generation, or with an explicit image
connection, is rejected; first reassign the image selection and remove that connection.

### Updating the previous configuration shape

Move `providers.<id>.api` and `baseUrl` into `providers.<id>.chat` for chat,
and put any custom image URL under `providers.<id>.images.baseUrl`. For built-in
platforms, set `type` and omit default connection settings. Replace the old
`imageGeneration.provider` and string `model` with
`imageGeneration.model: { provider, id }`; move any custom image API override to
`providers.<id>.images.api`. The old fields are rejected, not silently ignored.
Existing model catalogues can be retained as optional metadata or removed.
See [config.example.yaml](../config.example.yaml) for the new shape.

`runtime.executable` is `auto` by default, or an executable path. YAML relative
paths resolve against the configuration file directory. `ETS_RUNTIME_HOST_PATH`
takes precedence. CLI, Editor native sessions and Runtime MCP use this setting.

The DevKit package owns YAML I/O, provider and service configuration. Agent owns
its configuration schema and adapters for the model registry and credentials.
Browser responses contain explicit model summaries, never the whole YAML or API keys.
