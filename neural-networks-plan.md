# Neural networks: training and native inference

Status: deferred. Recorded 2026-09-28. Do not begin implementation until the
user explicitly resumes this work. This document describes planned capabilities,
not currently supported engine features.

## Objective

Make it straightforward to train models with Python and use them in DemiEngine,
especially for NPC behaviour. Provide a general inference service that other
game systems can reuse, with optional NPC-specific tooling layered above it.
Shipped games must not require Python or NVIDIA hardware.

## Agreed direction

- Start with offline Python training and native runtime inference.
- Add a complete demonstration-recording and imitation-learning example.
- Follow with engine-connected reinforcement-learning tooling as a separate
  phase. Training during normal gameplay is a further, explicit decision.
- Prefer an established inference library. ONNX Runtime is the leading
  candidate, not a finalized dependency decision. Recheck its target support,
  licensing, operator coverage, build footprint and accelerator options when
  implementation starts.
- Keep conventional gameplay rules, navigation and physics authoritative. A
  learned policy can propose actions; game code validates and applies them.
- Use small structured observations for the first NPC example. Image-based
  perception and language-model dialogue are different workloads and are not
  implied by this initial scope.

## Phase 1: Python-to-engine inference

- [ ] Select and pin an inference runtime after evaluating Linux and Android
  support. Retain a backend boundary without building speculative adapters.
- [ ] Define model assets using normal manifests, stable asset IDs, explicit
  loading/unloading, deterministic cooking and audited packaging. Keep generated
  artifacts out of authored source directories.
- [ ] Validate model inputs/outputs, names, shapes, data types, operator support
  and version compatibility during import. Report unsupported combinations
  clearly rather than silently changing the model.
- [ ] Record the observation/action contract alongside the model: field order,
  normalization, units, action meanings and any recurrent-state requirements.
  Python training and engine inference must use the same contract.
- [ ] Expose reusable Lua inference APIs with copied or explicitly owned buffers,
  asynchronous results, batching, error reporting and documented cancellation.
  Integrate with worker tasks without blocking the game thread or exposing live
  world storage to inference threads.
- [ ] Share model weights across compatible agents; keep per-agent recurrent
  state separate. Define lifetime and reload behaviour for in-flight requests.
- [ ] Provide editor import, model inspection and input/output diagnostics so
  using a model does not require hand-editing JSON.
- [ ] Supply a reproducible Python training/export script and an engine example.
  Compare exported inference results against the Python model on fixed inputs.

## Phase 2: NPC demonstration workflow

- [ ] Record observations and demonstrated actions into a versioned dataset
  format. Explain that observations alone do not specify desired behaviour.
- [ ] Train a small policy in Python, export it and import it through Phase 1.
- [ ] Demonstrate one useful behaviour, such as selecting attack/cover/retreat,
  with gameplay-side action validation and a conventional fallback.
- [ ] Keep the inference service independent of the NPC component. Support
  configurable decision frequency, batched observations and stale-result checks
  when an entity disappears or its situation changes before inference finishes.
- [ ] Evaluate on held-out encounters, not just training scenarios. Compare
  behaviour quality and authoring/debugging effort against a conventional policy.
- [ ] Document dataset provenance, reproducible settings and model evaluation.

## Phase 3: Engine-connected training

- [ ] Design a Python-facing simulation interface for observations, actions,
  rewards, reset and episode termination/truncation.
- [ ] Support controlled stepping, reproducible seeds and parallel training
  environments without tying training throughput to visible rendering.
- [ ] Reuse a maintained Python training library where suitable. Validate the
  interface before selecting a particular reinforcement-learning algorithm.
- [ ] Add training progress, checkpoints, evaluation and model-import workflows.
  Editor tooling may launch a Python process; do not embed a Python dependency
  into every shipped game just to support developer training tools.

## Later decision: learning during gameplay

Do not include this implicitly in inference support. Before implementing it,
agree on the use case, training budgets, writable model/checkpoint storage,
rollback/versioning, reproducibility, privacy and acceptable behaviour changes.
Training must not compete unchecked with gameplay for CPU, GPU or memory.

## Performance and reliability requirements

- CPU execution is a portable baseline, not a permanent CPU-only restriction.
  Benchmark available GPU/NPU providers on actual supported devices before
  choosing defaults; small models may not benefit from accelerator overhead.
- Coordinate runtime thread pools with the engine's workers. Do not create one
  inference session/thread pool per NPC by default.
- Profile observation gathering, marshaling, queueing, inference, result
  application and memory separately. Report latency and frame-time effects,
  including contention with rendering/physics, rather than FPS alone.
- Benchmark representative NPC counts and decision rates. Do not promise a
  population limit or impose arbitrary content caps before measuring.
- Provide configurable budgets and useful diagnostics for overload. Any
  deferred or dropped decisions must follow a documented game-selected policy.
- Cover malformed models, invalid tensors, unavailable accelerators, load
  failures, shutdown, scene changes and model reload while requests are active.
- Do not assume identical floating-point decisions across hardware backends.
  Define authority/replay requirements for games that need deterministic or
  network-authoritative behaviour.

## Completion criteria

- A developer can train/export in Python, import through the editor, run from
  Lua, inspect errors/performance and package a game without installing Python
  on the player's machine.
- A recorded-demonstration example proves the full workflow with documented
  behaviour evaluation and a fallback policy.
- Linux and physical Android qualification record actual hardware, model,
  backend, batching and decision frequency. Unsupported targets are explicit.
- Schemas, validators, editor workflows, stubs, examples and maintained website
  documentation agree. Publish guidance under
  tools/package-store/templates/docs/content/ and register pages in Docs.php;
  retain benchmark/architecture evidence under docs/.

## References to revisit

- ONNX Runtime C++: https://onnxruntime.ai/docs/get-started/with-cpp.html
- Mobile deployment: https://onnxruntime.ai/docs/tutorials/mobile/
- Thread management: https://onnxruntime.ai/docs/performance/tune-performance/threading.html
- Python model export: https://docs.pytorch.org/docs/stable/onnx.html
- Training-environment precedent: https://unity-technologies.github.io/ml-agents/ML-Agents-Overview/
