/*
  Download sources for the library browser.

  Every language has several mirrors (official + domestic CDNs). The fastest
  one is picked automatically by probing in parallel, and the user can pin a
  source manually from the window.
*/

#pragma once

#include <string>
#include <vector>

enum class LibLang
{
    Cpp = 0,
    Python = 1
};

struct LibSource
{
    LibLang lang;
    const wchar_t *name;  // shown in the window
    const wchar_t *base;  // base URL, always ends with '/'
    const wchar_t *probe; // small file used for the latency probe
};

const std::vector<LibSource> &AllSources(LibLang lang);

// Probes every source of that language in parallel and returns the index of
// the fastest reachable one (-1 when none answers). latencies is filled with
// the per-source result in the same order (-1 = unreachable).
int LibChooseFastest(LibLang lang, std::vector<long> &latencies);

// Remembers the last auto-chosen index per language.
int LibSelectedSource(LibLang lang);
void LibSetSelectedSource(LibLang lang, int index);
