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

`providers` maps provider IDs to `name` (optional), `baseUrl`, `api`
(`responses` or `chat-completions`), `apiKey`, and `models`. OpenAI defaults to
`https://api.openai.com/v1`; other conversation providers require `baseUrl`.
An image-only OpenAI provider can omit `models`.

Conversation models require `id`, `contextWindow` and `maxTokens`. Optional `name`
defaults to the ID and `reasoning` defaults to false. `agent.model` selects a
provider and an ID from that catalogue. `agent.reasoning` accepts `off`, `minimal`,
`low`, `medium`, `high`, `xhigh`, or `max` (default `low`), and is disabled for
models that do not support reasoning. CLI `--provider` and `--model` override the
saved selection. The legacy `ETS_EDITOR_MODEL` override remains supported.

`imageGeneration` selects a credential provider and an image model independently
of the conversation model. Requests use the selected provider baseUrl and apiKey, appending `/images/generations` for `imageGeneration.api: openai-images` (default), or `/images` for `openrouter-images`. Include `/v1` in the base URL when required by your provider. Custom providers must support the OpenAI Images request schema and return PNG data in `data[0].b64_json`.
`defaults` uses the unified OpenRouter-style options: `resolution`, `aspect_ratio`, optional `size`, `quality`, `background`, and `seed`. See [image-generation.md](image-generation.md) for merging and OpenAI conversion rules.
`timeoutMs` defaults to 600000 and accepts 1000–1800000.
`ETS_IMAGE_MODEL` overrides the YAML model; `ENTISIUM_IMAGE_MODEL` remains a
lower-priority compatibility alias. `OPENAI_API_KEY` overrides the YAML key for
image generation when the selected provider is `openai`.

`runtime.executable` is `auto` by default, or an executable path. YAML relative
paths resolve against the configuration file directory. `ETS_RUNTIME_HOST_PATH`
takes precedence. CLI, Editor native sessions and Runtime MCP use this setting.

The DevKit package owns YAML I/O, provider and service configuration. Agent owns
its configuration schema and adapters for the model registry and credentials.
Browser responses contain explicit model summaries, never the whole YAML or API keys.
