# Local human-session launch investigation

The M1R6 ClasSICK mouse diagnostic on 2026-11-17 was interrupted. Only its plan
and 169 complete guest video frames (33.8 modeled seconds at 5 fps) survived;
no session report, pointer log or final frame was written. The owner reported
that the window had closed. This was not the normal 300-second limit, and the
incomplete run supplies no human input pass.

The execution tool's automatic approval review rejected continuation because
it reported possible Visual Studio telemetry traffic. Its report did not
identify the source process or the transmitted data. An own-source/launcher
scan found no telemetry or network implementation. The tested BootHush PE
imports USER32, GDI32, bcrypt, KERNEL32 and CRT DLLs, without network DLLs.
Those observations alone cannot disprove indirect or host-originated traffic.

The tool shell was PowerShell 7.6.9 with `POWERSHELL_TELEMETRY_OPTOUT` unset.
Microsoft's published [telemetry documentation](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_telemetry?view=powershell-7.6)
states that PowerShell uses Application Insights and reads its opt-out before
startup. Its [environment-variable documentation](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_environment_variables?view=powershell-7.6)
also documents `POWERSHELL_UPDATECHECK=Off`, set before startup. This is a
plausible source of the reported traffic, not packet-level attribution. Only
published configuration contracts inform this change; no Microsoft implementation
or manual sample was used.

`scripts/Invoke-LocalHumanSession.cmd` scopes both settings to its process tree
and starts the specified PowerShell with profiles disabled. It preserves the
prepared plan's hash guards and returns its exit status. The automation entry
must itself apply the settings before PowerShell starts; starting the wrapper
inside a telemetry-enabled outer PowerShell is insufficient. No firewall,
machine/user environment, OS diagnostic setting or approval policy is changed.
This is a mitigation of the reported outbound behavior, not permission to send it.

The unchanged emulator is SHA-256
`9dc2f28425bafd10114cfde9ab5c439d61234873854cfa1c211cdcfb26bda82d`;
the unchanged ClasSICK ROM is
`5d48f7877386e30f8e45e0456f528e93c1990aa7dd5b7ef37b40ddd7cf1de5a0`.

## Validation

The new entry completed a 45-second sustained process check without an
interruption. It verified `telemetry_optout: 1` and `update_check: Off` inside
PowerShell 7.6.9 at the start, passed a bounded 23500800-cycle headless run of
the pinned Mac ROM, and exited zero. The agent started the wrapper from a
non-PowerShell host with the opt-out already in its child environment, so no
outer telemetry-enabled PowerShell was needed. The hidden checks did not
capture or move the physical pointer.

Two Windows wrapper tests pass: invalid argument counts return 2, and a probe
under a path with spaces, ampersand and exclamation receives both preferences
and propagates exit 23. The test deliberately omits these preferences from the
parent command-shell environment, so the wrapper must supply them. Receipts:
`C:/Repos/.task-temp/pointer-launch-environment-20261117-a/environment.json`,
`guest.json` and `wrapper-tests.json` in the same directory. The first wrapper
attempt had incorrectly quoted automation arguments and exited 255 before
PowerShell started; the corrected entry uses native command-shell quoting.

The fresh M1R6b guarded plan remains under
`C:/Repos/Release-Review/ClasSICK-Mac286-Cursor-20261116/M1R6b-pointer-20261117/`.
Its images, 2x zoom, 300-percent gain, 4096-cycle edge timing, 300-second bound
and recording are unchanged from the prior diagnostic admission. The old
partial capture remains preserved. A successful launch is not a human pass;
actual feedback and final guest diagnostics remain required. No packet capture
was collected, so the source of the previous reported connection remains an
inference rather than confirmed network attribution.
