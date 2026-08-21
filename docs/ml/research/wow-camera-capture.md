# Research: WoW 3.3.5 camera, spectator, and capture tooling

**Ticket:** [Research: WoW 3.3.5 camera, spectator, and capture tooling](https://github.com/gamesh411/mod-playerbots/issues/36)
**Map:** [Duel RL lunch seminar](https://github.com/gamesh411/mod-playerbots/issues/35)
**Scope:** filming spectated bot-vs-bot duels (warrior vs mage) in a stock 3.3.5a client (build 12340) on a local AzerothCore server, recorded with OBS on Windows 11, edited later with ffmpeg.

## Question

What client CVars, server GM commands, and OBS settings give us cinematic-looking duel footage, and which retail-era tricks do NOT exist on 3.3.5?

## Recommended baseline setup

This is the copy-paste starting point for the shot-list prototype.
Everything here is verified either against the 3.3.5 FrameXML dump or against the local AzerothCore source (details and sources in the sections below).

### Client one-time Config.wtf lines (client closed while editing)

```
SET gxWindow "1"
SET gxMaximize "1"
SET farclip "1277"
SET horizonfarclip "2112"
SET groundEffectDensity "256"
SET groundEffectDist "140"
SET environmentDetail "1.5"
SET particleDensity "1"
SET weatherDensity "3"
SET textureFilteringMode "5"
SET gxMultisample "4"
SET maxfps "60"
SET cameraDistanceMax "50"
```

Borderless-maximized windowed mode (`gxWindow` + `gxMaximize`) is deliberate: it is the stable capture mode on Windows 11 and avoids exclusive-fullscreen alt-tab blackouts.
`maxfps 60` matches the 60 fps recording target for even frame pacing.

### Client per-login macro (camera + clean world)

```
/console cameraDistanceMax 50
/console cameraSmoothStyle 0
/console cameraWaterCollision 0
/console cameraTerrainTilt 0
/console cameraBobbing 0
/console cameraPivot 0
/console nameplateShowEnemies 1
/console nameplateShowFriends 0
/console ShowClassColorInNameplate 1
/console UnitNameOwn 0
/console UnitNameFriendlyPlayerName 0
/console UnitNameEnemyPlayerName 0
/console enableCombatText 0
```

The camera-distance CVar sometimes fails to stick across sessions, so re-run this after login (or install the Camera_Max_Distance_Fix addon).
Decide per shot whether damage numbers help readability: `/console CombatDamage 1` and `/console CombatHealing 1` show them, both 0 for sterile footage.
Then Alt+Z to hide the whole UI - note Alt+Z does NOT hide nameplates or floating names, which is why they are configured separately above.
Since a duel makes the two bots hostile to each other but the observer is neutral to both, verify in practice which bot counts as "enemy" for nameplate purposes and adjust V / Shift+V (enemy / friendly plates) live.

### Server GM command sequence for the observer character (verified in local AzerothCore source)

```
.gm on
.gm visible off
.gm fly on
.modify speed all 2
.wchange 0 0
```

- `.gm visible off` makes the observer invisible to the bots and any players.
- `.gm fly on` works on a GM character in 3.3.5 - it sets the fly movement flag directly (`SetCanFly`), no mount needed, and enables elevated crane-style shots.
- `.modify speed all X` (range 0.1 to 50) scales walk, run, swim, and fly speed together - low values (0.3 to 0.5) give smooth dolly-like moves, high values reposition between takes.
- `.wchange <type> <grade>` sets zone weather: type 0 fine, 1 rain, 2 snow, 3 storm/sand (also 86 thunders, 90 black rain), grade 0.0 to 1.0 intensity - requires weather enabled in worldserver.conf.
- There is NO time-of-day GM command in this codebase - lighting follows the server clock, so schedule shoots (or change the server host clock) for golden-hour lighting.

### OBS settings (Windows 11, D3D9 client)

| Setting | Value |
|---|---|
| Source | Game Capture, mode "Capture specific window", pointed at Wow.exe |
| Fallback source | Window Capture with "Windows 10 (1903 and up)" WGC method |
| Encoder | NVENC H.264 (x264 CRF 16-18 preset slow if no NVIDIA GPU) |
| Rate control | CQP, CQ 15-18 (16 is a good default) |
| Preset | P5 or higher |
| FPS | 60 CFR |
| Keyframe interval | 1 s |
| Color | NV12, Rec. 709, Limited range |
| Canvas / output | match the WoW window 1:1 (Lanczos downscale only if delivering smaller) |
| Container | MKV, remux to MP4 after recording (or Hybrid MP4 on OBS 30.2+) |

Environment checklist: pin obs64.exe and Wow.exe to the same GPU in Windows graphics settings (hybrid-GPU black-screen fix), Windows HDR off, Xbox Game Bar overlay off, no RivaTuner/Discord/GeForce overlays, never mix Game Capture and Display Capture in one scene.
Our earlier finding that programmatic screenshot tools fail on this client (DWM cloaking) does not affect OBS Game Capture: it hooks the D3D9 Present call inside the game process and copies the backbuffer before DWM is involved, so cloaking is irrelevant.

## 1. Camera CVars and commands (3.3.5a specifics)

The single best primary source is the actual 3.3.5 client FrameXML dump (InterfaceOptionsPanels.lua, Bindings.xml), which proves which CVars the 12340 client wires up.

### Zoom distance

- The 3.3.5 names are `cameraDistanceMax` and `cameraDistanceMaxFactor` - the retail name `cameraDistanceMaxZoomFactor` is a 7.1.0 rename and does not exist on 3.3.5.
- `cameraDistanceMax`: default 15, accepted range 0 to 50 - `/console cameraDistanceMax 50` is the whole max-zoom trick on WotLK.
- `cameraDistanceMaxFactor`: multiplier, in-game slider only goes 1.0 to 2.0, console accepts more, but the effective product is capped around 50 yards anyway.
- Setting methods that all work on 3.3.5: `/console cameraDistanceMax 50`, `/script SetCVar("cameraDistanceMax", 50)`, or `SET cameraDistanceMax "50"` in Config.wtf.
- Quirk: opening the camera options panel re-clamps the value and it may not persist across sessions - re-apply after login, or use the Camera_Max_Distance_Fix addon built for 3.3.5.

### Smoothing and auto-adjust

- `cameraSmoothStyle` is the "Camera Following Style" dropdown: 0 never adjust, 1 adjust horizontally when moving, 2 always adjust, 4 adjust only when moving ("smarter").
- For filming set 0 so the camera never fights manual framing.
- Supporting speed CVars exist on 3.3.5: `cameraYawSmoothSpeed` (90 to 270), `cameraPitchSmoothSpeed`, `cameraYawMoveSpeed` (default 230), `cameraDistanceSmoothSpeed` (default 8.33, mouse-wheel zoom speed).
- The four camera checkboxes on the 3.3.5 options panel are real CVars: `cameraTerrainTilt` (default 0), `cameraBobbing` (default 0), `cameraWaterCollision` (default 1), `cameraPivot` (default 1).
- `cameraWaterCollision 0` lets the camera pass through the water plane - the classic fix for camera popping when filming near shorelines.
- `cameraPivot 0` stops the ground-collision pivot behavior.
- Terrain and building collision itself cannot be disabled by any 3.3.5 CVar - only the water plane and pivot are configurable, so pick shot positions accordingly.

### Field of view

- There is NO FOV CVar in the stock 3.3.5 client - `cameraFov` was added to retail in 9.2.5, and `SetCVar("cameraFov", x)` on 12340 does nothing.
- FOV control on 3.3.5 requires client patching: the awesome_wotlk patch (adds a working `cameraFov` CVar) or the external WoW Machinima Tool (FOV slider).
- Treat FOV as fixed for the baseline shot list and get "wide" or "tight" framing with camera distance instead.

### Version trap

- WotLK Classic (3.4.x) runs the modern engine, so WotLK Classic guides (factor max 4.0, `cameraDistanceMaxZoomFactor`, `nameplateMaxDistance`, DynamicCam) do NOT apply to real 3.3.5a.

Sources:
- 3.3.5 FrameXML dump: https://github.com/wowgaming/3.3.5-interface-files
- https://warcraft.wiki.gg/wiki/CVar_cameraDistanceMax
- https://warcraft.wiki.gg/wiki/CVar_cameraDistanceMaxZoomFactor
- https://warcraft.wiki.gg/wiki/CVar_cameraSmoothStyle
- https://warcraft.wiki.gg/wiki/CVar_cameraWaterCollision
- https://warcraft.wiki.gg/wiki/CVar_cameraFov
- WotLK-era CVar list (WoWWiki fork): https://addonstudio.org/wiki/WoW:Console_variables
- https://www.mmo-champion.com/threads/624242-how-to-set-max-camera-distance
- https://forum.warmane.com/showthread.php?t=431521
- https://github.com/d3m37r4/Camera_Max_Distance_Fix

## 2. UI hiding, nameplates, names, combat text, graphics

### Hiding the UI

- Alt+Z is the default `TOGGLEUI` binding in 3.3.5 and is implemented as a UIParent hide/show plus menu closing (verified in 3.3.5 Bindings.xml).
- `/script UIParent:Hide()` is equivalent, but with the UI hidden there is no chat box - restore with Alt+Z, not a slash command.
- Alt+Z does NOT hide nameplates or floating over-head names, because those render in the world (WorldFrame), not in UIParent - they must be turned off via their own toggles.

### Nameplates

- V toggles enemy nameplates (`nameplateShowEnemies`), Shift+V friendly (`nameplateShowFriends`), Ctrl+V both - all pure CVar toggles in 3.3.5.
- Extra sub-toggles exist for pets, guardians, and totems (`nameplateShowEnemyPets` etc.).
- `ShowClassColorInNameplate 1` class-colors enemy player health bars - nice for warrior-vs-mage legibility.
- There is NO nameplate distance CVar on 3.3.5 - render distance is hardcoded around 20 yards, and `nameplateMaxDistance` advice is TBC/WotLK Classic material.
- Stock 3.3.5 plates always draw name and level text - for bar-only plates use a 3.3.5 nameplate addon (TidyPlates/CleanPlates 3.3.5 ports exist on Warmane forums).

### Floating names and combat text

- Over-head names are the `UnitName*` CVar family, mapping 1:1 to Interface > Display > Names: `UnitNameOwn`, `UnitNameNPC`, `UnitNameFriendlyPlayerName`, `UnitNameEnemyPlayerName`, plus pet/guardian/totem and guild/PvP-title variants.
- World damage/healing numbers are engine CVars on 3.3.5: `CombatDamage`, `CombatHealing`, `PetMeleeDamage`, `CombatLogPeriodicSpells` (the `floatingCombatText*` prefix is a later rename that does not exist on 12340).
- Self scrolling combat text is `enableCombatText` plus the `fct*` sub-options and `combatTextFloatMode`.
- For sterile footage zero all of these - for readable "watch the numbers" shots keep `CombatDamage 1` and `CombatHealing 1`.

### Max graphics for 3.3.5 footage

- `farclip` max is 1277 on 3.3.5 (raised from 777 in 3.0.2, default 350) - `horizonfarclip` up to 2112.
- Ground clutter: `groundEffectDensity` up to 256 (default 16), `groundEffectDist` up to 140 (default 70).
- `particleDensity 1` is max on this era's 0.1 to 1.0 scale.
- `spellEffectLevel` (default 9) scales spell visual intensity, notably AoE missile counts (Blizzard, Rain of Fire) - directly relevant to the mage - raising it (community uses values up to the low hundreds) makes AoE lusher, test the FPS cost.
- Shadows: the 3.3.5 CVar is `shadowLOD` (default 1) - `shadowMode` is 4.0.1+ - pre-Cata shadows are blob shadows regardless, so do not chase shadow quality.
- `textureFilteringMode 5` is 16x anisotropic, `gxMultisample` 4 or 8 enables MSAA on the D3D9 device (needs a video-settings apply or relog).
- Full-screen effects: `ffxGlow 1` keeps the cinematic bloom, `ffxDeath 1` keeps the gray-out death effect for a dramatic kill shot (set 0 if it triggers on the observer instead).
- `weatherDensity 3` shows full weather effects, pairing with server-side `.wchange`.
- Frame pacing: `maxfps` (0 = uncapped) and `maxfpsbk` (background cap, default 30) - raise `maxfpsbk` if the capture client will be unfocused, `gxVSync` toggles vertical sync.
- UI scale if the UI stays visible: `useUiScale 1`, `uiScale` effective minimum 0.64.

Sources:
- 3.3.5 FrameXML dump (Bindings.xml, InterfaceOptionsPanels.lua): https://github.com/wowgaming/3.3.5-interface-files
- WotLK-era CVar lists: https://addonstudio.org/wiki/WoW:Console_variables/Complete_list and https://addonstudio.org/wiki/WoW:Console_variables/Complete_list/Character
- https://addonstudio.org/wiki/WoW:CVar_farclip
- https://addonstudio.org/wiki/WoW:CVar_spellEffectlevel
- https://addonstudio.org/wiki/WoW:CVar_uiScale
- https://warcraft.wiki.gg/wiki/CVar_shadowMode
- https://warcraft.wiki.gg/wiki/CVar_textureFilteringMode
- https://wowpedia.fandom.com/wiki/CVar_enableCombatText
- 3.3.5a Config.wtf guide: https://wotlkhelper.com/category/patches/client-optimization-via-configwtf
- Warmane threads: https://forum.warmane.com/showthread.php?t=24541 (graphics), https://forum.warmane.com/showthread.php?t=306310 (nameplate text), https://forum.warmane.com/showthread.php?t=351679 (nameplate range), https://forum.warmane.com/showthread.php?t=472471 (CleanPlates)

## 3. Observer and GM techniques (verified in local AzerothCore source)

All commands below were confirmed by reading the local checkout at `C:\AzerothCore\src\server\scripts\Commands\` - file references given per command.

- `.gm on` / `.gm off` - toggles GM mode (cs_gm.cpp).
- `.gm visible on|off` - invisibility toggle for the observer (cs_gm.cpp, HandleGMVisibleCommand).
- `.gm fly on|off` - calls `SetCanFly` on the selected player or self, so it works on the GM observer character in 3.3.5 without a mount and in any zone (cs_gm.cpp, HandleGMFlyCommand).
- `.gm spectator on|off` - this checkout additionally has a GM spectator flag (`SetGMSpectator`) - worth testing whether it improves duel-watching ergonomics (cs_gm.cpp, HandleGMSpectatorCommand).
- `.modify speed [all|walk|backwalk|swim|fly] <0.1-50>` - `all` sets walk, run, swim, and flight together, and bare `.modify speed X` maps to the same all-speed handler (cs_modify.cpp).
- Speeds around 0.3 to 0.5 with fly enabled give smooth dolly/crane moves - 3 to 10 for repositioning between takes.
- `.wchange <type> <grade>` - sets current-zone weather, types from SharedDefines.h: 0 fine, 1 rain, 2 snow, 3 storm, 86 thunders, 90 black rain, grade is a float intensity (cs_misc.cpp, HandleChangeWeather) - fails if weather is disabled in worldserver.conf.
- Positioning helpers: `.appear <name>` (teleport to a bot), `.summon <name>`, `.cometome` (cs_misc.cpp).
- `.freeze` / `.unfreeze` - can hold a selected bot in place for setup shots (cs_misc.cpp).
- `.morph <displayid>` / `.demorph` - observer disguise if it ever needs to be on camera (cs_modify.cpp).
- `.debug play cinematic <id>` / `.debug play movie <id>` - plays in-client cinematics, a curiosity for intro shots (cs_debug.cpp).
- `.spectator spectate|watch|leave|reset` - the ArenaSpectator system exists in this checkout (cs_spectator.cpp) but it explicitly requires a battle arena map, so it does NOT apply to open-world duels - relevant only if we ever stage fights in arenas instead.
- Time of day: no GM command exists in the command scripts - client lighting follows the server clock, so schedule recordings for the desired light or shift the host clock.

Practical note: the 3.3.5 camera is always tethered to the observer character (see section 4), so "camera moves" ARE observer moves - slow `.modify speed` plus `.gm fly` is the in-engine dolly, and `.gm visible off` keeps the rig out of frame for any other viewpoint.

## 4. Machinima tricks for 3.3.5

- The stock 3.3.5 camera is tethered to the character - there is no detached free camera without external tools.
- Smooth programmatic camera moves exist as protected-safe script APIs that work in 3.3.5 macros: `MoveViewInStart(0.15)` / `MoveViewOutStart`, `MoveViewLeftStart` / `MoveViewRightStart`, `MoveViewUpStart` / `MoveViewDownStart`, each with a `...Stop()` counterpart - speeds around 0.05 to 0.2 give a cinematic creep.
- Stop-everything macro: `/script MoveViewLeftStop() MoveViewRightStop() MoveViewUpStop() MoveViewDownStop() MoveViewInStop() MoveViewOutStop()`.
- Repeatable angles: `SaveView(2..5)` and `SetView(2..5)` (slot 1 is first person) - WotLK-era bug: saved views load only after a `/reload` on first login - `cameraViewBlendStyle` picks smooth blend vs instant cut between views.
- Freelook: scripting `CameraOrSelectOrMoveStart()` / `...Stop()` yaws the camera without turning the character.
- In-client viewpoint detachment is limited to spell tricks (Far Sight, Mind Vision, Eyes of the Beast) - situational at best for duels.
- If a true free-flight camera becomes necessary, the WotLK-era answer is the external WoW Machinima Tool 3.3.5 build (spectate mode, FOV, camera paths, slow motion, time-of-day, fog color), and modern options are the awesome_wotlk client patch (`cameraFov`, `nameplateDistance`) and Zen-Action-Cam-WOTLK on the ConsoleXP engine - all of these modify or inject into the client, so treat them as a later escalation, not the baseline.
- MaxCam (3.3.5 port exists) just automates max camera distance - our login macro covers the same thing.

Sources:
- https://wowvendor.com/media/wow/wow-camera-tricks-to-create-professional-footage-macros-and-addon/
- https://wowwiki-archive.fandom.com/wiki/API_SetView and https://wowwiki-archive.fandom.com/wiki/API_SaveView
- WMT: https://www.ownedcore.com/forums/world-of-warcraft/world-of-warcraft-bots-programs/347098-wow-machinima-tool-v3-24-a.html and https://www.wowmodding.net/files/file/356-wow-machinima-tool-335/
- https://github.com/FrostAtom/awesome_wotlk
- https://github.com/Zendevve/Zen-Action-Cam-WOTLK
- https://www.wowinterface.com/downloads/info24111-MaxCam.html

## 5. OBS capture of the 3.3.5 client on Windows 11

### Capture method

- Game Capture is the right source: it injects a hook into the D3D9 Present call and copies the backbuffer in-process, which is exactly why it works where PrintWindow/BitBlt-style tools fail on this client (those go through DWM/GDI and hit cloaking).
- 3.3.5 has no client anti-cheat to block hooking - reported black screens with WoW private-server clients trace to hybrid-GPU mismatches or overlay conflicts, not hook refusal.
- Use "Capture specific window" (not "any fullscreen application") since the client runs borderless windowed.
- Fallback order if Game Capture misbehaves: Window Capture with the WGC method, then Display Capture (always works, captures everything).
- Known failure modes: hybrid-GPU laptops need OBS pinned to the same GPU as the game (Windows graphics settings, High performance, then reboot) - overlays (RivaTuner, Discord, GeForce, Xbox Game Bar) can break the hook - "SLI/Crossfire capture mode" is a slow last-resort compatibility toggle - never combine Game Capture and Display Capture in one scene.

### Windowed mode

- Run borderless-maximized windowed via Config.wtf: `SET gxWindow "1"` plus `SET gxMaximize "1"` (gxMaximize only applies when gxWindow is 1) - both confirmed 3.3.5-era CVars.
- On Windows 11, exclusive fullscreen for a legacy D3D9 app brings alt-tab blackouts and mode-switch flicker for no benefit, and Fullscreen Optimizations wrap it into borderless presentation anyway.

### Recording settings

- NVENC H.264, rate control CQP with CQ 15-18, preset P5+ - x264 CRF 16-18 preset slow is the CPU alternative.
- Never CBR for local recording - constant quality (CQP/CRF) is what an edit master wants.
- 60 fps CFR (OBS default output is CFR, which ffmpeg and editors prefer), keyframe interval 1 s for fine stream-copy cut points.
- NV12, Rec. 709, Limited range - I444 only if a 4:4:4 master is ever needed.
- Canvas equal to the game window for a 1:1 master, Lanczos if downscaling.
- Record to MKV and remux to MP4 in OBS afterward, or use Hybrid MP4 (OBS 30.2+) - plain MP4 is unreadable after a crash.

### Windows 11 quirks

- Keep Windows HDR off for this SDR game (SDR capture comes out dim/washed with HDR on).
- Hardware-accelerated GPU scheduling (HAGS) is a documented cause of capture issues - toggle it off if capture misbehaves.
- Game Mode on is fine (OBS recommends it), but disable the Xbox Game Bar overlay.
- If the monitor runs above 60 Hz, cap the game near a multiple of 60 (`maxfps 60` or 120) to avoid judder in the 60 fps recording, and keep the OBS preview collapsed while recording.
- Leave "Capture Cursor" on unless a shot needs a cursor-free frame (the observer's cursor is normally parked off-action anyway).

Sources:
- https://obsproject.com/kb/game-capture-troubleshooting
- https://obsproject.com/kb/window-capture-sources
- https://obsproject.com/kb/hybrid-mp4
- https://obsproject.com/kb/hags
- https://obsproject.com/forum/threads/for-capture-method-whats-the-difference-between-bitblt-and-windows-graphics-capture.127687/
- https://obsproject.com/forum/resources/obs-studio-color-space-color-format-color-range-settings-guide-test-charts.442/
- https://obsproject.com/forum/resources/how-to-fix-mp4-mov-files-corrupting-when-obs-studio-crashes.1293/
- https://wowwiki-archive.fandom.com/wiki/CVar_gxMaximize
- https://support.microsoft.com/en-us/windows/optimizations-for-windowed-games-in-windows-11-3f006843-2c7e-4ed0-9a5e-f9389e535952
- https://freeradical.se/tools/obs/best-obs-recording-settings-2026.html

## Open items for the shot-list prototype

- Verify empirically which of the two dueling bots shows an enemy nameplate from the neutral observer's perspective, and whether `CombatDamage` numbers between two other players render for the observer.
- Test `.gm spectator` to see what it actually changes for a duel observer in this checkout.
- Test `spellEffectLevel` values above default for the mage's AoE visuals and measure the FPS cost.
- Confirm the OBS Game Capture hook on the actual machine (hybrid GPU pinning first if the screen is black).
- Decide whether the baseline tethered-camera moves are enough or whether WMT/awesome_wotlk free-camera escalation is warranted.
