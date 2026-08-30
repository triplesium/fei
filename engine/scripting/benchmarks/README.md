# Luau ECS query benchmark

This benchmark separates the main costs paid by Luau systems that iterate ECS
queries:

- native `DynamicQuery` iteration and field access;
- direct C++ implementations of the same numeric, property, and nested-property
  workloads used by the Luau cases;
- C++ implementations of those workloads through pre-resolved reflection
  `Property` objects, separating reflection cost from the Luau binding cost;
- the Luau VM loop and native function-call baseline;
- raw C iterators that return either a number or the same reused userdata on
  every row, isolating the per-row VM/native iterator boundary;
- native `DynamicQuery::next` with and without `field_untracked`, isolating ECS
  component-field lookup from the Luau bridge;
- pushing each queried component into Luau as borrowed userdata;
- reusable non-escaping query userdata versus the allocation fallback required
  when a queried component escapes its loop iteration;
- compiler-selected 32-row query chunks for low-cost, non-escaping loops,
  reducing native refills while preserving the original `for value in query`
  source form;
- reflected component property reads and writes;
- nested reflected access such as `component.position.x`, both repeated directly
  and with the intermediate object cached in a Luau local.

Build and run it in release mode:

```sh
xmake f -m release
xmake build -y entisium-scripting-query-benchmark
xmake run entisium-scripting-query-benchmark
```

Use `--entities=N`, `--samples=N`, and `--sample-ms=N` to change the workload.
`--csv` produces output suitable for comparing revisions. For example:

```sh
xmake run entisium-scripting-query-benchmark --entities=100000 --csv
```

The per-row cases reuse an already prepared query. `query/prepare` measures the
additional archetype refresh performed when a dynamic system parameter is
prepared for a system invocation.

The compiler selects chunked iteration only when borrow analysis proves that a
component local can be reused and the loop has at most two lowered property
accesses per row. Loops containing `break`, property-heavy loops, escaping
component values, and queries wider than the chunk helper supports fall back to
the ordinary row iterator.

Compare `luau/query component bridge` with its `row fallback` case for a
same-process measurement of the compiler-selected chunk path versus the
ordinary reusable row iterator.

Compare `cpp/direct`, `cpp/reflection`, and `luau` cases with the same suffix to
measure equivalent work at each layer. Compare the direct and locally cached
nested cases to isolate repeated traversal and the cost of creating borrowed
userdata for intermediate reflected objects. Write cases alternate between two
values on every invocation so that they measure real mutation and change-tick
tracking rather than the unchanged-value fast path. Human-readable and CSV
output report matched `vs_cpp_direct` and `vs_cpp_reflection` ratios.
