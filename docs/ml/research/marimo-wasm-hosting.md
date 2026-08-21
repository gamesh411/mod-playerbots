# Research: marimo WASM export constraints (packages, mobile, size)

Resolves issue #37 (wayfinder for map issue #35, Duel RL lunch seminar).
Researched 2026-08-21 against docs.marimo.io, the marimo GitHub repo, and Pyodide docs/issues.

## Recommended baseline

- Plotting library: Altair as the primary plotting library, with matplotlib as an acceptable fallback for purely static figures.
Altair has marimo's deepest integration (reactive selections, custom data transformers) and renders on the frontend via vega-lite JS, so the Python side only ships small data - a good fit for phone CPUs.
Avoid plotly in WASM: its wheel is heavy and it adds meaningful download and startup cost for no benefit at our scale.
- Package set: `marimo` (current release, pinned), `numpy`, `altair`, `pandas` (Altair's dataframe backend).
All are available in Pyodide (numpy, pandas, matplotlib, scikit-learn, scipy ship prebuilt; pure-Python deps come via micropip).
Lint compatibility before export with `marimo check notebook.py --select MW`.
- Export command: `marimo export html-wasm notebook.py -o dist --mode run --no-show-code` (run mode = read-only app view, editor chrome hidden - the clean audience experience).
- Pages deploy: fork or copy `marimo-team/marimo-gh-pages-template` into the new public repo, drop the notebook into `apps/` (run mode), set Pages source to "GitHub Actions", and keep a `.nojekyll` file at the site root.
- Phone viability verdict: viable.
Current Pyodide works in iOS Safari and Android Chrome (the early-2025 iOS crash in Pyodide 0.27.1/0.27.2 was fixed), the 2 GB WASM memory cap is far above what six numpy toys need, and the roughly 30 MB one-time download is cached on second load.
Mitigation: put the QR up at the start of the talk so the audience loads during the intro, not at the demo moment.

## 1. Export recipe

Command shape: `marimo export html-wasm notebook.py -o output_dir [options]`.

Key flags:

- `--mode run` produces a read-only app view; `--mode edit` produces an editable in-browser notebook.
- `--show-code / --no-show-code` controls whether code is initially visible.
- `--watch` re-exports on change during authoring.
- `--include-cloudflare` emits Cloudflare deploy config (not needed for Pages).
- `--execute` runs the notebook at export time and bundles cached cell outputs; combined with `cache_cells = true` under `[tool.marimo.runtime]` in `pyproject.toml`, cached cells hydrate from the bundle instead of recomputing in the browser.

Output layout: an HTML entry point plus an `assets/` directory, with `public/cache/` when `--execute` is used and `public/wheels/` when local wheels are bundled.
The export must be served over HTTP (`file://` does not work); test locally with `python -m http.server` from the output directory.
No special COOP/COEP headers are required for GitHub Pages hosting.

GitHub Pages: the official route is the template repo `marimo-team/marimo-gh-pages-template`.
Notebooks in `notebooks/` are exported with `--mode edit`, notebooks in `apps/` with `--mode run`; a build script (`.github/scripts/build.py`, runnable locally via `uv run .github/scripts/build.py`) exports everything into `_site/` and generates an index page, and the bundled GitHub Actions workflow uploads that as the Pages artifact (deploy job with `pages: write` and `id-token: write` permissions).
Configure the repo's Pages source as "GitHub Actions" and include `.nojekyll` in the site root so Jekyll does not mangle the assets.

Sources:
- https://docs.marimo.io/guides/exporting/webassembly_html/
- https://docs.marimo.io/guides/publishing/github_pages/
- https://github.com/marimo-team/marimo-gh-pages-template

## 2. Package constraints under Pyodide

- Preinstalled scientific stack: numpy, scipy, scikit-learn, pandas, and matplotlib all ship as prebuilt Pyodide wheels.
Additional compiled packages such as duckdb and polars are also available.
- Pure-Python packages: anything with a pure-Python wheel on PyPI installs via micropip; marimo auto-installs missing imports, or you can call `await micropip.install("pkg")` explicitly.
- Platform markers: PEP 508 markers with `sys.platform == "emscripten"` let one requirements set serve both local and WASM runs.
- Known-broken in WASM, relevant to us: no OS threads or true CPU parallelism (`threading.Thread`, `ThreadPoolExecutor`, `multiprocessing.Pool` exist only as cooperative adapters; `threading.Lock`/`Semaphore` unsupported), no pdb, 2 GB memory limit, and network fetches are subject to browser CORS (bundle data files in `public/` instead of fetching).
None of this touches our six toys as long as each interaction is a single-threaded numpy computation, which is the plan.
- Plotting in WASM: matplotlib works (prebuilt) and renders static images; Altair renders through vega-lite on the frontend and is marimo's most sophisticated integration, with the `marimo_csv` data transformer handling 400k+ rows (we need thousands at most); plotly installs via micropip but ships a large bundle and is the one to avoid.
- Recent regression worth knowing: marimo 0.23.0-0.23.1 failed to initialize under Pyodide at all (a `|` union type in `altair_transformer.py`, issue #9152, filed April 2026, now closed/fixed).
Lesson: pin the marimo version and smoke-test the exported site on a phone after any bump.

Sources:
- https://docs.marimo.io/guides/wasm/
- https://pyodide.org/en/stable/usage/packages-in-pyodide.html
- https://github.com/marimo-team/marimo/issues/9152
- https://docs.marimo.io/guides/working_with_data/plotting/

## 3. Payload size and first-load time

- A marimo WASM app is roughly a 30 MB one-time download on first paint (Pyodide runtime ~10 MB plus marimo frontend assets plus per-package wheels: numpy is a few MB, pandas is the largest of our set).
- Cold start including download is roughly 5-30 seconds depending on network; on a mid-range phone over office wifi expect the upper half of that range, dominated by download plus WASM compile plus numpy/pandas import.
- Second load is much faster: the browser caches the wasm binaries and wheels, and init drops to a few seconds.
- What keeps it small: fewest packages possible (each import line costs a wheel), prefer Altair over plotly, skip scipy/sklearn unless a section truly needs them, and use `--execute` with `cache_cells` so expensive first-render outputs hydrate from the bundle instead of computing on the phone.
- GitHub Pages serves from a CDN, so 30 phones hitting the QR simultaneously is not a hosting concern - the constraint is each phone's own download.

Sources:
- https://bury-thomas.medium.com/deploy-a-python-dashboard-to-github-pages-with-marimo-no-backend-all-free-7bda3acee3ef
- https://docs.marimo.io/guides/exporting/webassembly_html/

## 4. Mobile behavior

- Official support statement: WASM notebooks are supported in the latest Chrome, Firefox, Edge, and Safari, with Chrome recommended for best performance.
- iOS Safari: works today.
The notable failure was Pyodide 0.27.1/0.27.2 crashing iOS Safari outright (pyodide/pyodide#5428, early 2025); it was fixed in a follow-up release, and current Pyodide (314.x line as of 2026) is fine.
This is exactly the class of regression to catch with a real-device smoke test before the talk.
- Memory: the 2 GB WASM cap coexists with iOS Safari's own tab memory pressure; keep per-section arrays small (thousands of floats, not millions) and phones will be comfortable.
- Layout: `--mode run` is the app view - no editor chrome, cells rendered as a clean vertical document, which stacks naturally on a narrow screen.
Slider polish is the main small-screen rough edge: slider width is not full-width by default (marimo-team/marimo#627, #8359) and rapid dragging can outrun the debounce (#4924).
Practical fixes: keep one slider per row via `mo.vstack`, give sliders explicit labels with the current value, and keep per-interaction compute under ~100 ms so drag feels live.
- Offline quirk: an exported wasm-html site did not fully load offline on iOS (marimo-team/marimo#5206) - irrelevant for us since the audience is online, but do not promise offline replay.

Sources:
- https://docs.marimo.io/guides/wasm/
- https://github.com/pyodide/pyodide/issues/5428
- https://github.com/marimo-team/marimo/issues/627
- https://github.com/marimo-team/marimo/issues/8359
- https://github.com/marimo-team/marimo/issues/4924
- https://github.com/marimo-team/marimo/issues/5206

## 5. Presenter parity (local `marimo run` vs WASM build)

- Semantics are identical: same reactive dataflow, same `mo.ui` widgets, same notebook file.
The differences are environmental.
- Performance: local CPython is several times faster than Pyodide on a phone; anything tuned to feel instant on the laptop should be budgeted at 3-10x slower for the audience.
Design each slider callback to be trivially cheap and it will feel the same in both places.
- Package versions: Pyodide pins specific versions of the compiled stack (numpy, pandas), which may differ from whatever the laptop venv resolves.
Pin the local venv to match the Pyodide-shipped versions to avoid cosmetic differences in numerical output.
- Randomness: `numpy.random` is deterministic given a seed on both platforms; seed every stochastic cell explicitly (`np.random.default_rng(SEED)`) so presenter and audience see the same trajectories.
Unseeded randomness will differ per device, which for the self-play and learning-curve sections could look confusing.
- Threads/subprocess/file IO: anything using them works locally and breaks in WASM; run `marimo check --select MW` and simply do not use them.
- Practical stage recipe: the presenter can open the very same GitHub Pages URL as the audience for guaranteed parity, keeping `marimo run notebook.py` on localhost only as the fallback if venue wifi dies.

Sources:
- https://docs.marimo.io/guides/wasm/
- https://docs.marimo.io/guides/exporting/webassembly_html/

## 6. Alternatives (flag only)

No disqualifying problem was found, so marimo WASM stands.
If a pre-talk phone smoke test ever surfaces a blocking Pyodide-on-iOS regression, the fallback would be plain HTML+JS (or quarto with observable cells) reimplementing the six toys in JavaScript - zero runtime download and instant load, at the cost of abandoning the Python source of truth.
Not recommended unless forced.
