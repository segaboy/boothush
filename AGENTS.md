# BootHush development

BootHush's project goal is a headless development and testing platform for portable software and ports across substantially different platforms. ClasSICK is the immediate development priority and a real integration partner, not the limit of the project. Read `docs/project-direction.md` before making architectural decisions.

Build reusable capabilities around real, tested needs: isolate machine-specific behavior from shared orchestration, input, observation, capture, and comparison when the interfaces can be justified by working profiles. Preserve and extend the Mac and 286 profiles, and continue toward executing the actual ClasSICK x64 UEFI guest. Do not claim an unsupported machine, automatic translation, hardware fidelity, or cross-platform equivalence without evidence. A Super Nintendo to Panasonic 3DO port is a future example, not an implemented workflow.

Keep automated runs headless and bounded, avoid taking the user's desktop or input devices, preserve input media by default, and distinguish scheduled input, window-message injection, host input, replay, and guest-observed behavior. Report capabilities and determinism limits for each backend honestly. The owner's 2026-11-16 direction requires an opt-in human testing path where needed or requested. Maintain the Mac/286 live window and evidence launcher alongside headless automation; never infer human verification from synthetic messages or automatically opening a window. See `docs/human-testing.md`.

ClasSICK development uses this repository's local checkout to discover BootHush capabilities. When changing a command, supported machine behavior, diagnostic format, test procedure, or known limitation, update the relevant README and integration documentation in the same commit. Keep examples runnable and distinguish tested behavior from planned work.

Use `docs/classick-integration.md` as the entry point for the headless ClasSICK integration. Link to detailed device documentation rather than duplicating complete specifications. Keep generated ROMs, disk images, traces, and other test outputs out of Git. Use original or properly licensed test material.
