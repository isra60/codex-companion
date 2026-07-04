# Reference Projects

These projects are useful references even though they target Claude Code rather than Codex:

- https://github.com/HermannBjorgvin/Clawdmeter
- https://github.com/alonw0/claude-monitor-esp32

## Ideas Adopted Now

- Keep the hardware firmware focused on the device while the desktop service owns host-side Codex integration.
- Add an explicit smoke-test path so hardware and dashboard states can be tested without running a real agent session.
- Treat attention states differently from passive states. Permission requests should be visually dominant and should eventually get optional sound/haptic feedback.

## Ideas Deferred

- Board HAL split like Clawdmeter. We only target one board today, but the firmware should be refactored before adding more displays.
- Battery and charging indicator through AXP2101. Useful once hardware is connected and measured.
- Brightness presets and idle dimming. AMOLED burn-in and desk use make this worthwhile.
- Audio alerts through the onboard codec. This needs hardware validation and should stay optional.
- Host installer / diagnostic scripts. Useful after the firmware compiles and flashes successfully on a real board.

## Ideas Not Adopted

- BLE as the main transport. Codex Companion already has a LAN WebSocket service and the user requested mDNS discovery.
- ESP32-hosted HTTP server for hooks. The current companion service is safer because hooks stay local to the PC and permissions fail closed if the approval path breaks.
