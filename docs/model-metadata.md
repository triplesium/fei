# Model metadata

DevKit's `ModelMetadataService` queries model information independently of model
execution. CLI and Editor each share one service between chat and image generation.
`config.yaml` still needs only a provider connection and `{ provider, id }` selection.
Neither fetching a catalogue nor selecting a model writes discovered data to YAML.

## Platform adapters

- OpenRouter fetches `/models?output_modalities=all` from the capability's base URL.
  It maps names, modalities, context length and supported parameters. The catalogue's
  `top_provider` limits remain under `topProvider`; they are not treated as limits
  guaranteed by every upstream route. See the [OpenRouter model API](https://openrouter.ai/docs/guides/overview/models).
- OpenAI fetches `/models` with the selected stored API key. Its standard response
  provides basic identity information; missing capabilities come from bundled data
  or overrides. Without a key, discovery is unavailable and bundled data remains
  usable. See the [OpenAI model API](https://developers.openai.com/api/reference/resources/models).
- fal queries `https://api.fal.ai/v1/models`, using `endpoint_id` for a selected
  model. Image generation requests `expand=openapi-3.0`. The original schema document
  is retained; for a single POST operation its JSON request and success-response
  schemas are also exposed. References stay in the original document and are not
  fetched or executed. Listing supports cursors and omits schemas by default. See
  the [fal model API](https://fal.ai/docs/platform-apis/v1/models).
- Generic `openai-compatible` connections do not assume a metadata extension.
  Their discovery status is `unsupported`; direct model IDs, overrides and applicable
  built-in presets continue to work.

## Resolution and runtime behavior

Each field merges from user overrides, remote metadata, then bundled metadata.
Missing properties remain unknown. Explicit false values are preserved. The result
records field sources (`override`, `remote`, `builtin`) and remote fetch timestamps.
Query status distinguishes `found`, `not-found`, `unavailable` and `unsupported`.
A model missing from a catalogue is still selectable.

The Agent boundary supplies operating defaults only where facts remain unknown:
32,768 context tokens, 4,096 maximum output tokens (clamped to context), and disabled
reasoning. Those values are not added to the metadata cache as provider facts.
OpenRouter's primary-route output limit is retained for inspection, not used as a
universal maximum. A partial YAML override can set the desired operating limit:

```yaml
providers:
  router:
    type: openrouter
    apiKey: your-api-key
    models:
      - id: vendor/model
        maxTokens: 8192
        # Other fields are discovered automatically.
```

Agent startup initializes local settings without network discovery. Resolving the
selected model before use performs a bounded query when needed. Opening the model
selector loads catalogues asynchronously; **Refresh models** bypasses cache freshness.
Downloaded image-only models are excluded from the chat selector. Resolved model
objects are snapshots: refreshing the registry does not mutate an object already
handed to a running caller.

Images query metadata after validating the destination and credentials, before
generation. An explicitly incompatible output modality is rejected; missing data or
discovery failure does not block the existing image adapter. Metadata/OpenAPI schemas
do not automatically add execution support for arbitrary fal endpoints. Parameter
mapping, queue execution and output decoding remain model-adapter responsibilities.

## Cache and failures

The default TTL is one hour and request timeout is five seconds. Fresh cache entries
avoid network requests. Expired entries are returned immediately while one shared
refresh runs in the background. A forced refresh waits for its result. Failures retain
old entries and apply a 30-second retry delay; cancelling one caller does not cancel
another caller's shared request. Responses are limited to 8 MiB and redirects are disabled.

Cache identities hash the provider ID, platform, actual metadata base URL, credential
scope and query options. Credentials are not written to the cache. Different keys or
URLs cannot reuse each other's entries. Corrupt cache files are ignored; unavailable
disk storage falls back to memory. Cache locations:

- Windows: `%LOCALAPPDATA%/Entisium/model-metadata/`
- Other systems: `$XDG_CACHE_HOME/entisium/model-metadata/`, or
  `~/.cache/entisium/model-metadata/`

Tests inject HTTP responses, time and temporary cache directories, so they do not
need real API keys or make billable generation calls.
