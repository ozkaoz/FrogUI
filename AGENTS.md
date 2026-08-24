# AGENTS.md — FrogUI — TreeFrogUI R36SX Fork Component

**Repo:** https://github.com/ozkaoz/FrogUI (fork), upstream `https://github.com/tzubertowski/FrogUI`
**Branch:** `sf3000`
**Protocol:** `TREEFROGUI_AGENT_PROTOCOL=1`
**Parent coordination:** `../../treefrog-ui-r36sx/docs/ai/MULTIREPO_COORDINATION.md` (via `/mnt/d/R36SX/treefrog-ui-r36sx` or `~/sf3000-work/treefrog-ui-r36sx-build`)

> Local rules for the **FrogUI** fork. Parent `treefrog-ui-r36sx/frogui` is a submodule checkout — do not edit there. All feature development happens here (`~/sf3000-work/FrogUI`).

## 1. Purpose

FrogUI is the frontend launcher (`frogui_libretro.so`) loaded by picoarch. This fork (`sf3000`/`r36sx`) adapts it for Hichip MIPS handhelds (R36SX 640×480 fbwrite, SF3000-family 854×480 dispframe), input via `cubevol → /tmp/joy_key` shm, and TreeFrogUI integration.

## 2. Canonical env

- **WSL Ubuntu** (same as parent). Windows path `D:\R36SX\FrogUI` → WSL `/mnt/d/R36SX/FrogUI` or `~/sf3000-work/FrogUI` (`/home/dafunknoise/sf3000-work/FrogUI`). Do not build in PowerShell.
- Toolchain `~/sf3000-work/sf3000toolchain/mipsel-buildroot-linux-gnu_sdk-buildroot` (`mips-mti-linux-gnu-gcc 6.3.0`), same as parent (`docs/BUILDING.md`).
- Parent workspace: `~/sf3000-work/{treefrog-ui-r36sx,FrogUI,TreeFrogUI_picoarch,cores,sf3000toolchain}`.

## 3. Branch discipline

- `sf3000` — TreeFrogUI SF3000/R36SX integration (parent submodule `see parent docs/PROJECT_STATE.md`, upstream `tzubertowski/FrogUI@sf3000`). Base for `r36sx-v2.6-dev`.
- `r36sx` — R36SX-specific (if used, see `upstream/r36sx`).
- `master` — upstream vanilla.
- Feature branches: `feature/r36sx-<topic>` from `sf3000` (e.g., `feature/fn-button-mapping-v1015`).
- Never `git push upstream` (tzubertowski) without auth; push to `origin` `ozkaoz/FrogUI` only. No `force push` after review.

## 4. Build

```sh
# WSL, in this repo (~/sf3000-work/FrogUI) — branch sf3000 b2c0cfc
sh build_libretro.sh  # → frogui_libretro.so (hardcodes /home/tomaszz/sf3000-work/FrogUI, see BUILDING.md debt)
# alternative at 15ea12b pin: make -f Makefile.sf3000 frogui_libretro.so (if present)
ls -lh frogui_libretro.so  # → copy to parent sdcard/cubegm/cores/
file frogui_libretro.so  # ELF 32-bit LSB MIPS32r2
```

Flags: `-mips32r2 -march=mips32r2 -mtune=24kc` (legacy, parent `build_all.sh` uses `74kc -mdspr2` — `BUILD_FLAG_CONSISTENCY=KNOWN_DEBT`, do not silently change).

Artifact: `frogui_libretro.so` → parent `treefrog-ui-r36sx/sdcard/cubegm/cores/` → `release/latest/release/cubegm/cores/` via `build_release.sh`.

## 5. R36SX / SF3000 input invariants

- Input: `hijack/zhijack.tpl.sh` `cubevol → /tmp/joy_key` shm, read by `frogui/input.c` (parent `docs/HARDWARE.md`).
- **R36SX V2.6 hardware facts (do not change without physical evidence):**
  - `FN raw bit 16 mask 0x00010000`, `L3 raw bit 1`, `R3 raw bit 2` (`frogui/input.c`, `tests/test_frogui_fn.py` in parent, `docs/HARDWARE.md`).
  - Right analog mirrors face buttons X/A/B/Y on/off (hardware wiring, no analog).
  - Preserve backwards compatibility: `frogui/settings.txt` keys, `picoarch.cfg` expectations, existing `input.h` bit definitions.
  - No invented libretro button IDs.

## 6. Tests

- No dedicated FrogUI unit tests in this repo (check `tests/` if added). Parent contract: `python tests/test_agent_context_contract.py` (parent) validates `FROGUI_SUBMODULE=see parent docs/PROJECT_STATE.md`.
- For UI changes: manual R36SX V2.6 physical validation required (`docs/TESTING.md` parent, `docs/ai/VALIDATION.md` CLASS C `STATIC+BUILD+HOST+PHYSICAL`).

## 7. Physical validation

- `BUILD PASS` (WSL cross-compile) ≠ `PHYSICAL PASS`. Agents never claim `PHYSICAL PASS` without human hardware report (`FIRMWARE/BASELINE`, `ARTIFACT_SHA256`, `TEST_MATRIX`, `USER_OBSERVATIONS`, `PASS/FAIL`, `DATE`).
- Cross-repo feature (FrogUI+picoarch) → parent integration `release/latest/release` + `PHYSICAL` matrix (see `docs/ai/MULTIREPO_COORDINATION.md` §3/§7).

## 8. Git safety

- `git commit`/`push`/`tag` = `ask`. No `reset --hard`, `clean -fd`, `restore` destructive, `rm -rf`, `force push`.
- Focused commits, no unrelated changes. Each repo separate history — no parent integration commit for child-only change.
- Verify `git status --porcelain`, `git rev-parse HEAD`, `git remote -v` before edit (parent `docs/ai/MULTIREPO_COORDINATION.md` §6).

## 9. Cross-repo handoff

After change, report to parent integration repo (`treefrog-ui-r36sx`):

```
REPOSITORY=FrogUI BRANCH=feature/r36sx-foo BASE=<pin, see parent docs/PROJECT_STATE.md>
ARTIFACT=frogui_libretro.so SHA256=... BUILD_RESULT=PASS
RELATED_PRS= https://github.com/tzubertowski/FrogUI/pull/...
NEXT_EXACT_ACTION= Parent assembles release/latest/release and human tests R36SX
```

See `docs/ai/MULTIREPO_COORDINATION.md` §4 handoff contract.

## 10. Protocol

```
TREEFROGUI_AGENT_PROTOCOL=1
```

Mismatch → update this file or parent coordination before proceeding.
