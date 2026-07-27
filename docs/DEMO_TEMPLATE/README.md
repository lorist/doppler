# &lt;your-demo&gt; — one-line tagline

> Copy this folder to `demos/<your-demo>/`, then replace this README and the
> `CMakeLists.txt`. See [`../adding-a-demo.md`](../adding-a-demo.md).

One short paragraph: **what slice of Pexip Pulse does this demo show, and why is
it interesting?** Keep it to one or two Pulse capabilities — the place for
breadth is `pexninja`.

```
(optional ASCII diagram of the data flow — see demos/doppler or demos/gateway
 for examples)
```

## What it highlights

* `pulse_…()` — the key Pulse call this demo is about.
* `pulse_…()` — and any supporting ones.

## Build & run

From the repository root (the Pulse runtime must be installed first — see the
[repository README](../../README.md#1-install-the-pexip-pulse-runtime)):

```bash
cmake -S . -B build -DBUILD_<YOUR_DEMO>=ON
cmake --build build -j --target <your-demo>
./build/run-<your-demo>.sh
```

(If your demo builds by default you can drop the `-DBUILD_<YOUR_DEMO>=ON`.)

## Code tour

A few sentences pointing at the interesting parts of `src/`. The house style is
heavily-commented source, so the code itself should carry most of the detail.
