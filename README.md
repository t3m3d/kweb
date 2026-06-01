# kweb — the Krypton Web Framework

Build websites and web apps in **pure Krypton**. Server-side HTML, a CSS DSL,
a client-JS DSL, and **KSML** (KryptScript Media Language) for interactive apps
with no hand-written JavaScript.

```krypton
import "k:htmk"
import "k:ksml"
import "k:server"

func page() {
    emit htPage("Hello", ksmlRuntime(),
        htH1("Click count: ") +
        htSpanA(htId("c"), "0") +
        htButtonA(ksmlPostInto("/inc", "#c"), "+1"))
}
```

That button does a real HTTP round-trip and swaps the new count into the page —
zero JavaScript written by you.

---

## What's in the box

| Module | What it does |
|--------|--------------|
| `k:htmk` | Server-side HTML DSL — 120+ helpers (`htDiv`, `htH1`, `htForm`, `htTable`, …). Escapes by default. |
| `k:ksml` | **KryptScript Media Language** — htmx-style hypermedia attributes. Make any element fire a request and swap part of the page. |
| `k:krouter` | Path router — `routeIs(method, path, "POST", "/todo/:id")` + `routeParam(...)`. `:name` params and `*` catch-all. |
| `k:ks`   | Client-side JavaScript DSL — emit JS from Krypton (`ksLet`, `ksOnClick`, `ksSetText`, …) when you want hand-rolled client logic. |
| `k:server` | HTTP server — request access (`serverReqPath`, `serverFormValue`, `serverQueryValue`), responses (`serverRespond`, `serverRespondJSON`), cookies, CORS. |

The `kweb` CLI (`kweb.htk`) wraps the common workflow: `kweb init <name>`,
`kweb build`, `kweb serve [port]`, `kweb deploy <host> <user>`.

---

## KSML — KryptScript Media Language

KSML brings the **hypermedia** model to Krypton: the server sends HTML, the
client swaps it in. It emits [htmx](https://htmx.org)-compatible attributes, so
the battle-tested htmx runtime drives them — you just generate the markup.

### Verbs · target · swap

```krypton
htButtonA(ksmlPost("/save") + ksmlTarget("#status") + ksmlSwap("innerHTML"), "Save")
// -> <button hx-post="/save" hx-target="#status" hx-swap="innerHTML">Save</button>
```

| Helper | Emits |
|--------|-------|
| `ksmlGet/Post/Put/Patch/Delete(url)` | `hx-get` … the request |
| `ksmlTarget(sel)` | `hx-target` — which element to update |
| `ksmlSwap(strategy)` | `hx-swap` — `innerHTML` / `outerHTML` / `beforeend` / … |
| `ksmlTrigger(event)` | `hx-trigger` — `click` / `change` / `every 2s` / … |

### Convenience

```krypton
ksmlGetInto(url, sel)      // GET, swap result into sel's innerHTML
ksmlPostInto(url, sel)     // POST, same
ksmlLivePoll(url, "5s")    // re-fetch every 5s, replace this element
ksmlSwapInner() / SwapOuter() / Append() / Prepend()
ksmlOnLoad() / Poll("2s") / OnChange() / OnSubmit()
ksmlIndicator(sel) / Confirm(msg) / PushUrl("true") / Boost("true")
```

### Runtime include

Drop the client runtime once in `<head>`:

```krypton
ksmlRuntime()              // pinned CDN copy of the htmx runtime
ksmlRuntimeLocal("/htmx.js")   // or a self-hosted copy you ship
```

---

## Routing

`k:krouter` matches request paths against patterns with named params, so
handlers read cleanly instead of hand-rolled `if/startsWith` chains:

```krypton
let m = serverReqMethod()
let p = serverReqPath()

if routeIs(m, p, "POST", "/todo/:id") {
    let id = routeParam("/todo/:id", p, "id")   // e.g. "3"
    ...
} else if routeIs(m, p, "GET", "/static/*") {
    let file = routeTail("/static/*", p)        // e.g. "css/app.css"
    ...
}
```

Patterns are slash-segmented: literal segments match exactly, `:name`
captures any one segment, a trailing `*` is a catch-all.

## Examples

```bash
KRYPTON_ROOT=. kcc examples/ksml_counter.htk -o counter && ./counter
# open http://localhost:8080
```

| File | Shows |
|------|-------|
| `examples/hello.htk` | Minimal htmk page + server. |
| `examples/interactive.htk` | Client-side interactivity via `k:ks` (hand-written JS from Krypton). |
| `examples/form.htk` | Form handling. |
| `examples/ksml_counter.htk` | A live counter — KSML round-trip, no JS. |
| `examples/ksml_todo.htk` | A full add/toggle/delete todo app, no JS. Uses `k:krouter` + `serverFormValue`. |
| `examples/ksml_search.htk` | Live active-search — debounced type-to-filter, results swapped in. No JS. |

---

## How a KSML app works

1. Browser loads the page (`serverRespond(page())`). The page includes
   `ksmlRuntime()` once.
2. An element with KSML attributes (e.g. `ksmlPostInto("/inc", "#c")`) fires an
   HTTP request on its trigger (default: click / submit).
3. Your server handles the route and returns **just an HTML fragment**
   (`serverRespondText(...)` or `serverRespond(htUl(...))`).
4. KSML swaps that fragment into the page at `hx-target` using `hx-swap`.

No JSON, no client state machine, no build step for the client. The server is
the single source of truth; the page is hypermedia.

---

## Requirements

- A Krypton toolchain (`kcc`). See https://github.com/t3m3d/krypton.
- Build kweb sources with `KRYPTON_ROOT` pointing at this repo so `k:htmk`,
  `k:ksml`, etc. resolve from `stdlib/`.

## Status

kweb ships on Windows as a bundled `kweb.exe` (in the Krypton 2.1.1 installer).
On macOS / Linux, build from source as shown above. The `k:server` socket layer
uses `#ifdef _WIN32` Winsock vs POSIX sockets, so it runs on all three.

---

Built in Krypton. No LLVM, no Node, no framework underneath — just the language.
