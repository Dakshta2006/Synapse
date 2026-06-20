# 🛠️ Synapse - AI Coding Assistant CLI

A blazing-fast, terminal-native AI coding assistant built in C++17. `Synapse` reads your codebase structure, maps file relationships into a dependency graph, ranks relevant files using an AST-aware keyword & graph-proximity algorithm, and interacts with LLMs to automatically apply context-aware diffs with absolute safety (featuring automated in-memory backups and undo).

---

## ⚡ Technical Architecture & Code Flow

The execution workflow supports two distinct modes (Standard and Multi-Agent Planning) and features a compile verification loop:

```mermaid
graph TD
    A[User CLI Input] -->|--prompt / --dir| B[Phase 1: CLI Parser]
    B --> C[Phase 2: Tree-sitter AST Parser]
    C -->|Extract Functions, Imports, Classes| D[Phase 3: Dependency Graph builder]
    
    subgraph Standard Mode
        D -->|Build Graph & Run Proximity Ranker| E[Phase 3: Ranker Output top files]
        E --> F[Phase 4: LLM Payload Constructor]
        F --> G[Phase 4: Query LLM for Edits]
        G --> H[Phase 5: Parse and Apply Patches]
    end

    subgraph Multi-Agent Planning Mode
        D -->|Serialize Graph| K[Phase 1: Planner Agent]
        K -->|JSON Steps Plan| L[Phase 2: Execution Agent]
        L -->|Iterate Steps| M[Single-File Edit Queries]
        M --> H
    end

    H --> N{--build-cmd specified?}
    N -->|Yes| O[Phase 6: Auto-Compile & Fix Loop]
    O -->|Build fails| P[Query LLM for compiler fixes]
    P -->|Apply patches| O
    O -->|Build succeeds| Q[Done]
    N -->|No| Q
    
    H -->|Backup Original File| I[Patched file write via std::filesystem]
    I -->|Undo command| J[Restore original file]
```

### Module Breakdown

#### 📂 Phase 1: CLI Entry & Environment (`/cli/src/main.cpp`)
- Built using `cxxopts` to parse options.
- Initializes parameters: target codebase directory (`--dir`), prompt instruction (`--prompt`), and advanced modes:
  - `--divide`: Enables Multi-Agent Planning (Divide & Conquer) mode.
  - `--build-cmd`: Command to run to check compilation.
  - `--max-fixes`: Max iterations to fix errors (default: 3).

#### 📂 Phase 2: AST Analysis (`/cli/src/parser.cpp`)
- Traverses ASTs dynamically to extract functions, classes, and imports.

#### 📂 Phase 3: Graph Construction & Graph Ranking (`/cli/src/graph.cpp`)
- Generates dependency nodes, resolves imports to paths, and propagates weights.
- Supports **Graph Serialization** to provide a concise ASCII graph representation for the Planner Agent.

#### 📂 Phase 4: Network Payload & LLM Request (`/cli/src/client.cpp`)
- Communicates with the Gemini API to get raw edits, step-by-step plans, or compile fixes.

#### 📂 Phase 5: Diff Application & Safety (`/cli/src/patcher.cpp`)
- Parses SEARCH/REPLACE blocks and applies updates to files securely with automatic backup support (`.bak` files).

#### 📂 Phase 6: Auto-Compile & Fix Loop (`/cli/src/main.cpp`)
- Executes the user-provided compilation command after patches are applied.
- If compile fails, extracts offending source files from build logs and requests fix patches recursively from the LLM.

---

## 🛠️ How to Build (Windows)

We use **vcpkg** in **Manifest Mode** (`vcpkg.json`) to automate dependency installs, coupled with **CMake**.

### Prerequisites
- Windows 10/11
- Visual Studio Build Tools with C++ compiler and CMake support.

### Compilation Steps

1. **Bootstrap vcpkg**:
   Ensure `vcpkg.cmake` toolchain file path is known.

2. **Configure and Build**:
   Use CMake to configure and compile (e.g. using the Ninja or Visual Studio generator):
   ```powershell
   cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake"
   cmake --build build --config Release
   ```
   This generates `my_ai.exe` inside the build directory.

---

## 💻 CLI User Interface

```text
=========================================================
 🛠️  Synapse : AST-Guided Coding Assistant
=========================================================
Usage:
  my_ai.exe [OPTION...]

  -p, --prompt arg     User prompt for the AI (Required)
  -d, --dir arg        Target directory (default: .)
  -f, --file arg       Test AST parser on a specific file
  -u, --undo           Rollback the last applied changes
  -t, --top arg        Number of top relevant files to include in context
      --divide         Enable multi-agent planning (divide and conquer) mode
      --build-cmd arg  Compilation/build command to run for the fix loop
      --max-fixes arg  Max number of compiler fix iterations (default: 3)
  -h, --help           Print this help message
```

### Multi-Agent Planning & Fix Loop Output Example:

```text
[~] Scanning AST & Parsing directory: ./src ...
[✓] Found 14 C++ files.
[~] Building dependency graph...
[~] Matching query "validateEmail" to AST identifiers...
    -> Match found in registration.cpp (Weight: 1.0)
    -> Spreading weights to neighbors (db_helper.cpp, auth.h)...
[!] Top 3 Context Files Selected:
    1. src/registration.cpp (Score: 1.00)
    2. src/auth.h           (Score: 0.50)
    3. src/db_helper.cpp     (Score: 0.25)

[~] Fetching suggestions from LLM...
[✓] AI suggests modification in src/registration.cpp.
[~] Backup created: src/registration.cpp.bak
[✓] Patched src/registration.cpp successfully!
    (Run './my_ai --undo' to restore)
```

---

## 🚀 Advanced Optimizations & Roadmap

To make `Synapse` compile, run, and scale faster than typical Python-based alternatives, we can implement the following enhancements:

### 1. Incremental AST Parsing & Caching
- **Problem**: Scanning hundreds of files on every execution takes time.
- **Solution**: We can hash the contents of files (e.g., using a quick MD5 or MurmurHash3) and serialize the AST output to a local SQLite database or JSON file. Next time `Synapse` runs, it will only parse files whose hashes have changed.
- **Tree-sitter feature**: Tree-sitter supports incremental parsing (re-parsing only modified lines).

### 2. Multi-threaded Parsing
- Walking the directory structure and parsing ASTs is highly parallelizable. We can use C++17 `std::execution::par` or thread pools to parse multiple files concurrently across all CPU cores.

### 3. Context Window Optimization (Token Pruning)
- LLMs charge by the token and slow down with huge prompts.
- Instead of sending the *entire* text of the top 3 files, `  Synapse` can extract *only* the relevant class/function bodies using Tree-sitter coordinates, pruning unused helper functions and boilerplates to maximize instruction density.
  
### 4. Hybrid Graph + Vector Search
- Currently, ranking uses string matches on identifiers (variables, functions, imports) and spreads weights across graph edges.
- **Future**: We can integrate a small C++ embeddings library (like `llama.cpp` or vector quantization) to parse semantic intent (e.g., matching "verify user is logged in" to `check_auth_session`).

### 5. Multi-Language Extensibility
- Tree-sitter uses standard parser libraries written in C. We can support multi-language environments (Python, JS, Go, Rust, C++) by compiling their respective grammar files (`parser.c`) straight into our project or linking them dynamically.
