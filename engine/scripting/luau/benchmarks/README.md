# Luau ECS query benchmark

This benchmark separates the main costs paid by Luau systems that iterate ECS
queries:

- native `DynamicQuery` iteration and field access;
- the Luau VM loop and native function-call baseline;
- pushing each queried component into Luau as borrowed userdata;
- reflected component property reads and writes;
- nested reflected access such as `component.position.x`, both repeated directly
  and with the intermediate object cached in a Luau local.

Build and run it in release mode:

```sh
xmake f -m release
xmake build -y entisium-scripting-luau-query-benchmark
xmake run entisium-scripting-luau-query-benchmark
```

Use `--entities=N`, `--samples=N`, and `--sample-ms=N` to change the workload.
`--csv` produces output suitable for comparing revisions. For example:

```sh
xmake run entisium-scripting-luau-query-benchmark --entities=100000 --csv
```

The per-row cases reuse an already prepared query. `query/prepare` measures the
additional archetype refresh performed when a dynamic system parameter is
prepared for a system invocation.

Compare the direct and locally cached nested cases to isolate the cost of
creating borrowed userdata for intermediate reflected objects.
