mergeInto(LibraryManager.library, {
  ets_profile_wasm_function_index__deps: ["$getWasmTableEntry"],
  ets_profile_wasm_function_index: function(tableIndex) {
    const fn = getWasmTableEntry(tableIndex);
    if (!fn) return 0xffffffff;
    const match = /^(?:wasm-function\[)?(\d+)/.exec(fn.name);
    if (!match) return 0xffffffff;
    const functionIndex = Number(match[1]);
    return Number.isSafeInteger(functionIndex) ? functionIndex : 0xffffffff;
  },

  ets_profile_wasm_build_id__deps: ["$lengthBytesUTF8", "$stringToUTF8"],
  ets_profile_wasm_build_id: function(output, capacity) {
    const buildId = globalThis.ETS_PROFILE_BUILD_ID || "";
    const length = lengthBytesUTF8(buildId);
    if (!output || !capacity || length + 1 > capacity) return 0;
    stringToUTF8(buildId, output, capacity);
    return length;
  },
});
