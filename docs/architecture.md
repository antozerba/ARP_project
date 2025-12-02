# Project Architecture

This document provides a high-level architecture diagram of the project and a short explanation of components and relationships. The diagram is in Mermaid syntax and can be rendered by editors that support Mermaid (VS Code Mermaid Preview, GitHub, GitLab, etc.).

```mermaid
flowchart TB
  %% Subgraphs per tipo
  subgraph Core [Core Logic]
    Main[main.c]
    Server[server.c]
  end

  subgraph Generators [Generators]
    TG[target_gen.c]
    OG[obstacles_gen.c]
  end

  subgraph IO [Input/Output]
    Input[input.c]
    Window[window.c]
  end

  subgraph Utilities [Utilities]
    Utils[utils.c]
  end

  %% Flussi
  Main -->|starts| Server
  Main -->|starts| Input
  Main -->|starts| Window
  Main -->|starts| Dynamic
  Main -->|starts| OG
  Main -->|starts| TG

  TG -->|generate targets| Server
  OG -->|generate obstacles| Server
  Input -->|send commands| Server
  Dynamic -->|compute dynamic| Server
  Server -->|update state| Window
  Server -->|resize command| OG
  Server -->|resize command| TG
  Utils -->|helpers| Server
  Utils -->|helpers| Window
  Utils -->|helpers| Input
  Utils -->|helpers| Dynamic
  Utils -->|helpers| TG
  Utils -->|helpers| OG

  %% Styling
  classDef core fill:#f0f8ff,stroke:#333,stroke-width:1px;
  classDef gen fill:#fff0f5,stroke:#333,stroke-width:1px;
  classDef io fill:#f5fff0,stroke:#333,stroke-width:1px;
  classDef util fill:#fff8dc,stroke:#333,stroke-width:1px;

  class Main,Server core;
  class TG,OG gen;
  class Input,Window,Dynamic io;
  class Utils util;
```

Structure:
This is a conceptual diagram based on the project repository. It shows the typical roles each entity likely plays:
  - `main.c` is the entry point and likely starts all the processes.
  - `server.c` contains the core logic and coordinates data between target and obstacles generators, input, window and dynamic
  - `input.c` captures user/input events and forwards them to the server logic.
  - `target_gen.c` and `obstacles_gen.c` generate data fed into the server (targets/obstacles).
  - `window.c` handles rendering/displaying state produced by the server using the lib ncurses.
  - `utils.c` provides shared helper functions used across components to load configuration and logging,
  - `config/parameters.txt` provides runtime parameters.
  - `log/` stores runtime logs.

How to render
- Mermaid: view `docs/architecture.md` with a Mermaid-capable viewer.