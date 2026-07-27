# Adding a new Pulse demo

This repo is meant to grow. Every demo is a small, self-contained advert for
some slice of Pexip Pulse, and they all follow the same shape so a newcomer can
pick any of them up in seconds. This guide walks through adding your own.

> Read [`../AGENTS.md`](../AGENTS.md) first for the big picture of how the repo
> and the Pulse SDK fit together.

## The shape of a demo

```
demos/<your-demo>/
├── README.md          What it shows + how to build & run (2–3 commands).
├── CMakeLists.txt      A handful of lines using the PulseDemo.cmake helpers.
└── src/
    └── main.cpp        Your demo. Heavily commented is the house style.
```

A ready-to-copy skeleton lives in [`DEMO_TEMPLATE/`](DEMO_TEMPLATE/).

## Step by step

### 1. Scaffold from the template

```bash
cp -r docs/DEMO_TEMPLATE demos/<your-demo>
```

Then put your code under `demos/<your-demo>/src/`.

### 2. Write the `CMakeLists.txt`

The parent project already found Pulse, GLFW/OpenGL and Dear ImGui and built the
shared `imgui` target, so a demo only declares its own executable and reuses the
helpers from `cmake/PulseDemo.cmake`:

```cmake
add_executable(<your-demo> src/main.cpp)
target_link_libraries(<your-demo> PRIVATE
    imgui
    pexip::pulse
    Threads::Threads)
pulse_demo_rpath(<your-demo>)
pulse_demo_launcher(<your-demo> run-<your-demo>.sh)
```

If your demo needs extra packages (X11, PJSIP, ImPlot, ...), look at how
`demos/pexninja/CMakeLists.txt` and `demos/sip/CMakeLists.txt` pull theirs in
via `find_package` / `pkg_check_modules` / `FetchContent`.

### 3. Register it in the root `CMakeLists.txt`

Add an option and an `add_subdirectory()`:

```cmake
option(BUILD_<YOUR_DEMO> "Build the <your-demo> demo" ON)   # OFF if it needs extra deps
...
if(BUILD_<YOUR_DEMO>)
    add_subdirectory(demos/<your-demo>)
endif()
```

Default the option **ON** only when the demo builds with the same dependencies
as `doppler` (just GLFW + OpenGL). If it needs anything extra, default it
**OFF** and document the dependency in the demo's README.

### 4. Write the `README.md`

Keep it to: a one-paragraph "what this shows", an optional ASCII diagram, the
Pulse calls it highlights, and a **build & run** section that is two or three
commands. Mirror the existing demo READMEs.

### 5. Add it to the showcase index

Add a row to the demo table in the top-level [`README.md`](../README.md) so the
new demo is discoverable.

## Checklist

- [ ] Lives in `demos/<your-demo>/` and nothing outside it moved.
- [ ] Has a `README.md` whose build & run is 2–3 commands.
- [ ] Uses `pulse_demo_rpath()` + `pulse_demo_launcher()` so it runs without
      hand-set library paths.
- [ ] Registered with a `BUILD_<DEMO>` option (ON only if no extra system deps).
- [ ] Listed in the top-level README's demo table.
- [ ] Builds: `cmake -S . -B build -DBUILD_<YOUR_DEMO>=ON && cmake --build build -j --target <your-demo>`.
