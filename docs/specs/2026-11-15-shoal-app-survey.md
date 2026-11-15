# Shoal friendly mode: survey of the 13 O&C v1.3.6 apps

This survey is read from the source in `src/shoal/firmware/`. Line numbers refer to those files as vendored, including the Shoal edits. Apps appear in `available_apps` order (`OC_apps.ino` 54-68).

**File names.** All match the brief except Sequins, which is in **`APP_A_SEQ.ino`**. There is no `APP_SEQ.ino`. There is also no `OC_pattern_edit.h`; the pattern editor is `OC_sequence_edit.h`, with data in `OC_patterns.*`.

**Shoal never sends the screensaver events.** It never sends `APP_EVENT_SCREENSAVER_ON` or `_OFF` (`o_c_REV.ino` 150-151), and a long press on UP does nothing (`OC_ui.cpp` 159-166). So in every app, the screensaver handling noted under point 8 is inert in Shoal.

## Where the generic access falls short (summary)

`SettingsAccess.inc` maps the right object and settings count for every app. `setSettingRaw`, however, is a bare `apply_value`, so it skips each app's guards and follow-ups. The details are under each app's points 5 and 6; the list below collects them.

### Data no settings row reaches

- **Automatonnetz cell settings:** 25 cells × 4 settings in `cells_[]`.
- **Sequins step pitches:** in `OC::g_user_patterns[8]`.
- **Acid Curds chord content:** in `OC::g_user_chords[32]`.
- **User scales:** `OC::g_user_scales[4]`, shared by all apps.

### Follow-ups the encoder path performs that a raw write skips

- **Rebuild of the visible-row list and cursor range** (`update_enabled_settings` + `cursor.AdjustEnd`), needed after a raw write to:

  | App | Settings |
  | --- | --- |
  | CopierMaschine | CV source |
  | Harrington 1200 | Trigger type |
  | Quantermain | Scale, CV Source, Trigger |
  | Meta-Q | SCALE1-4, "scale #", seq_mode, CV source, trigger, aux.output |
  | Piqued | delay mode, Eucl length |
  | Sequins | aux mode, play mode, direction |
  | Viznutcracker sweet | Loop mode |
  | Acid Curds | playmode, direction, scale |

- **Scale changes that fix up the mask and the preview variable** (`set_scale` + `left_encoder_value`): CopierMaschine and Acid Curds. In both, Scale is not a menu row: the L encoder previews it and an L push commits it.
- **Mask writes that need `update_scale_mask` / `scale_changed`:**
  - Sequins: a raw mask write has no effect at all.
  - Quantermain and Meta-Q: the quantizer picks the change up at the next trigger, but the stored copy of the mask is stale.
- **Other required calls:**
  - Meta-Q needs `reset_scale()` after a write to "scale #" or seq_mode.
  - Harrington 1200 needs `mode_change(true)` after a write to Root mode.
  - Sequins needs `pattern_changed()` after a step-mask write in ARP mode.
  - Sequins `DUMMY` has a member copy that a raw write does not update.

### Constraints a raw write ignores

- **Piqued Euclidean bounds:** offset ≤ length, fill ≤ length + 1, and length pulls fill and offset down with it.
- **Trigger/source combinations in Quantermain and Meta-Q:** the firmware only blocks the step of +1 from TR4 or CV4, so a larger jump gets through. Friendly mode should enforce the intent directly.

### Value names shorter than the setting's range

Reading the name for the out-of-range values runs past the end of the array, so friendly mode has to supply its own labels:

| App | Setting | Range | Names provided | Out-of-range values |
| --- | --- | --- | --- | --- |
| Piqued | trigger input | 0..6 | 4 | 4-6 are other channels' end-of-cycle triggers ("EOC") |
| Meta-Q | CV source | 0..4 | 4 | 4 is Turing |
| Acid Curds | `CHORD_SLOT` | 0..31 | 8 | 8-31 |

### Defaults that differ by channel

The table's default is the same for every channel, but `Init` sets different ones. Trigger input in Piqued, Dialectic Ping Pong and Viznutcracker sweet defaults to TR1..TR4 for channels A..D.

### Values lost on save and restore

References "Mod range oct" is -3..3 but stored as `STORAGE_TYPE_U8`, so negative values do not survive save and restore.

### Firmware guards that do nothing (verified)

- Meta-Q (`APP_DQ.ino` 1370) and Sequins (`APP_A_SEQ.ino` 2237) combine their `MASKn != setting` tests with `||`, so the condition is always true.
- Sequins `force_update()` sets a flag that nothing reads.

---

## CopierMaschine (`APP_ASR.ino`)

All paths below are relative to `src/shoal/firmware/`. App table entry: `OC_apps.ino:55` (`"CopierMaschine"`, `BORING_APP_NAMES` name `"ASR"` at `:39`).

### 1. Channels

- **One** settings object: `ASR asr` (`SHOAL_STATE(ASR, asr)`, `APP_ASR.ino:799`), class `ASR : settings::SettingsBase<ASR, ASR_SETTING_LAST>` (`:101`). UI state lives separately in `ASRState asr_state` (`:774-797`): `left_encoder_value` (pending scale), `cursor`, `scale_editor`.
- There is no channel selection. The four outputs A-D are four taps of a single ring buffer (`updateASR_indexed`, `:414-442`), all quantised with the same scale/root/mask. `SettingsAccess.inc` reports 1 channel for `asr`, which is correct.

### 2. Settings table (`SETTINGS_DECLARE(ASR, ...)`, `:744-770`; enum `:53-80`)

Constants: `NUM_SCALES` = `SCALE_USER_LAST (4) + sizeof(braids::scales)/sizeof(...)` (`OC_scales.cpp:12`). `braids_quantizer_scales.h` has 98 entries, so NUM_SCALES = 102 and the max scale is 101. `ASR_HOLD_BUF_SIZE` = 256/4 = 64 (`:38-39`). `NUM_INPUT_SCALING` = 40, `MULT_ONE` = 19 (`:40,51`). `kIntSeqLen` = 128 (`OC_strings.h:6`). `kNumDelayTimes` = 8 (`OC_strings.h:10`).

| idx | enum | name string | min | max | default | value names |
|---|---|---|---|---|---|---|
| 0 | `ASR_SETTING_SCALE` | `"Scale"` | 0 | `NUM_SCALES-1` (101) | `SCALE_SEMI` (5) | `OC::scale_names_short` (102 entries: `"USER1".."USER4"`, `"OFF "` (=`SCALE_NONE`, idx 4), `"SEMI"`, `"IONI"`, `"DORI"`, ... braids scales; `OC_scales.cpp:28`) |
| 1 | `ASR_SETTING_OCTAVE` | `"octave"` | -5 | 5 | 0 | NULL |
| 2 | `ASR_SETTING_ROOT` | `"root"` | 0 | 11 | 0 | `OC::Strings::note_names_unpadded` = `C C# D D# E F F# G G# A A# B` (`OC_strings.cpp:17`) |
| 3 | `ASR_SETTING_MASK` | `"mask"` | 1 | 65535 | 65535 | NULL (bit mask; U16) |
| 4 | `ASR_SETTING_INDEX` | `"buf.index"` | 0 | `ASR_HOLD_BUF_SIZE-1` (63) | 0 | NULL |
| 5 | `ASR_SETTING_MULT` | `"input gain"` | 0 | 39 | `MULT_ONE` (19) | `OC::Strings::mult`: 40 strings `"0.05".."2.00"` in 0.05 steps (`OC_strings.cpp:61-66`) |
| 6 | `ASR_SETTING_DELAY` | `"trigger delay"` | 0 | 7 | 0 | `OC::Strings::trigger_delay_times` = `off, 120us, 240us, 360us, 480us, 1ms, 2ms, 4ms` (`OC_strings.cpp:39-41`) |
| 7 | `ASR_SETTING_BUFFER_LENGTH` | `"hold (buflen)"` | 4 | 63 | 4 | NULL |
| 8 | `ASR_SETTING_CV_SOURCE` | `"CV source"` | 0 | 3 | 0 | `asr_input_sources` = `CV1, TM, ByteB, IntSq` (`:727-729`) |
| 9 | `ASR_SETTING_CV4_DESTINATION` | `"CV4 dest. ->"` | 0 | 4 | 0 | `asr_cv4_destinations` = `oct, root, trns, buf.l, igain` (`:731-733`) |
| 10 | `ASR_SETTING_TURING_LENGTH` | `"> LFSR length"` | 1 | 32 | 16 | NULL |
| 11 | `ASR_SETTING_TURING_PROB` | `"> LFSR p"` | 0 | 255 | 128 | NULL |
| 12 | `ASR_SETTING_TURING_CV_SOURCE` | `"> LFSR CV1"` | 0 | 3 | 0 | `OC::Strings::TM_aux_cv_destinations` = `-, rng, len, prb` (`OC_strings.cpp:68-70`). Value 1 ("rng") actually adds to input gain (`:517-519`). |
| 13 | `ASR_SETTING_BYTEBEAT_EQUATION` | `"> BB eqn"` | 0 | 15 | 0 | `OC::Strings::bytebeat_equation_names` = `hope, love, life, age, clysm, monk, NERV, Trurl, Pirx, Snaut, Hari, Kris, Tichy, Bregg, Avon, Orac` (`OC_strings.cpp:43-45`) |
| 14 | `ASR_SETTING_BYTEBEAT_P0` | `"> BB P0"` | 1 | 255 | 8 | NULL |
| 15 | `ASR_SETTING_BYTEBEAT_P1` | `"> BB P1"` | 1 | 255 | 12 | NULL |
| 16 | `ASR_SETTING_BYTEBEAT_P2` | `"> BB P2"` | 1 | 255 | 14 | NULL |
| 17 | `ASR_SETTING_BYTEBEAT_CV_SOURCE` | `"> BB CV1"` | 0 | 4 | 0 | `bb_CV_destinations` = `igain, eqn, P0, P1, P2` (`:735-737`) |
| 18 | `ASR_SETTING_INT_SEQ_INDEX` | `"> IntSeq"` | 0 | 9 | 0 | `OC::Strings::integer_sequence_names` = `pi, vnEck, ssdn, Dress, PNinf, Dsum, Dsum4, Dsum5, CDn2, Frcti` (`OC_strings.cpp:51-53`) |
| 19 | `ASR_SETTING_INT_SEQ_MODULUS` | `"> IntSeq modul"` | 2 | 121 | 24 | NULL |
| 20 | `ASR_SETTING_INT_SEQ_START` | `"> IntSeq start"` | 0 | `kIntSeqLen-2` (126) | 0 | NULL |
| 21 | `ASR_SETTING_INT_SEQ_LENGTH` | `"> IntSeq len"` | 2 | `kIntSeqLen` (128) | 8 | NULL. The getter returns value-1 (`:236-238`). |
| 22 | `ASR_SETTING_INT_SEQ_DIR` | `"> IntSeq dir"` | 0 | 1 | 1 | `OC::Strings::integer_sequence_dirs` = `swing, loop` (`OC_strings.cpp:55-57`) |
| 23 | `ASR_SETTING_FRACTAL_SEQ_STRIDE` | `"> Fract stride"` | 1 | `kIntSeqLen-1` (127) | 1 | NULL |
| 24 | `ASR_SETTING_INT_SEQ_CV_SOURCE` | `"> IntSeq CV1"` | 0 | 5 | 0 | `int_seq_CV_destinations` = `igain, seq, strt, len, strd, mod` (`:739-741`) |

Side note (upstream bug, unrelated to the menu): `:577-580` call `bytebeat_.set_p0` three times with p0, p1 and p2. As a result the generator's p0 ends up holding the "> BB P2" value, "> BB P0" and "> BB P1" have no effect, and the generator's p1/p2 are never set.

### 3. Visible settings

- The app builds an enabled list: `update_enabled_settings()` (`:370-412`), with `enabled_setting_at()` (`:292`) and `num_enabled_settings()` (`:296`). The menu cursor indexes into this list (`ASR_menu`, `:1003`).
- These rows are always shown, in this order (not enum order): `ASR_SETTING_ROOT, ASR_SETTING_MASK, ASR_SETTING_OCTAVE, ASR_SETTING_INDEX, ASR_SETTING_BUFFER_LENGTH, ASR_SETTING_DELAY, ASR_SETTING_MULT, ASR_SETTING_CV4_DESTINATION, ASR_SETTING_CV_SOURCE` (`:374-383`).
- The rest depends on `switch (get_cv_source())` (`:385-409`):
  - `case ASR_CHANNEL_SOURCE_TURING` (value 1) adds `TURING_LENGTH, TURING_PROB, TURING_CV_SOURCE`.
  - `case ASR_CHANNEL_SOURCE_BYTEBEAT` (value 2) adds `BYTEBEAT_EQUATION, BYTEBEAT_P0, BYTEBEAT_P1, BYTEBEAT_P2, BYTEBEAT_CV_SOURCE`.
  - `case ASR_CHANNEL_SOURCE_INTEGER_SEQUENCES` (value 3) adds `INT_SEQ_INDEX, INT_SEQ_MODULUS, INT_SEQ_START, INT_SEQ_LENGTH, INT_SEQ_DIR, FRACTAL_SEQ_STRIDE, INT_SEQ_CV_SOURCE`.
  - CV1 (0) adds nothing.
- **`ASR_SETTING_SCALE` is never a menu row.** It is selected with the L encoder and committed with an L press (see 4).
- There are no pages. The scale editor overlay is the only other screen.
- The `MASK` row draws the mask instead of a value (`:1009-1011`). The `CV_SOURCE` row, when Turing is selected, draws the live shift register next to the value (`:1013-1021`).

### 4. Controls (`ASR_handleButtonEvent` `:849-876`, `ASR_handleEncoderEvent` `:878-919`)

When the scale editor is active (`asr_state.scale_editor.active()`), every button and encoder event goes to the editor (`:850-853`, `:880-883`); see 6.

| control | action | state changed | setting? |
|---|---|---|---|
| L encoder | `left_encoder_value += value`, constrained to `0..NUM_SCALES-1` (`:885-889`). This only *previews* a scale: the title shows its name, and nothing changes in `asr`. | `asr_state.left_encoder_value` | UI member, not a setting (the pending value for `ASR_SETTING_SCALE`) |
| R encoder, not editing | `cursor.Scroll(value)` (`:916`) | cursor | no |
| R encoder, editing | `change_value(enabled_setting_at(cursor_pos), value)` unless the row is MASK (`:893-914`); see 5 | that setting | yes |
| L push | `ASR_leftButton` (`:949-953`): `if (left_encoder_value != asr.get_scale(DUMMY)) asr.set_scale(left_encoder_value)` commits the previewed scale | `ASR_SETTING_SCALE` (+ possibly `ASR_SETTING_MASK`) | yes (encoder-only function) |
| L long | `ASR_leftButtonLong` (`:955-961`): `set_scale(left_encoder_value)`, then `if (scale != SCALE_NONE) scale_editor.Edit(&asr, scale)`. Commits the scale and opens the mask editor. | SCALE (+MASK) + opens editor | yes + sub-editor |
| R push | `ASR_rightButton` (`:933-947`): on the MASK row, `if (SCALE_NONE != scale) scale_editor.Edit(&asr, scale)`; otherwise `cursor.toggle_editing()` | editor / cursor edit flag | no |
| UP | `ASR_topButton` (`:922-927`): `if (asr.octave_toggle()) change_value(ASR_SETTING_OCTAVE, 1) else change_value(ASR_SETTING_OCTAVE, -1)`. This alternates +1 / -1 on each press. `octave_toggle_` flips even when `change_value` hits a bound (`:159-162`), so the toggle can get out of step with the octave. | `ASR_SETTING_OCTAVE` + member `octave_toggle_` | octave yes, toggle no (not saved) |
| DOWN | `ASR_lowerButton` (`:929-931`): `asr.manual_freeze()` toggles `freeze_switch_` (`:280-282`). TR2 edges also set and clear it in `update()` (`:495-500`). | member `freeze_switch_` | no (not saved; `clear_freeze()` on restore `:820`) |
| DOWN long | `ASR_downButtonLong` (`:963-965`): `toggle_delay_mechanics()` flips `delay_type_`. When true, all four taps read the same delay offset spacing 0, i.e. adjacent samples (`:434`). | member `delay_type_` | no (not saved; reset to false in `init()` only) |
| L long via OC_ui | reaches the app (`OC_ui.cpp:172-173`) | see above | |
| UP long | does nothing in Shoal (`OC_ui.cpp:159-166`, SHOAL edit) | | |
| R long | opens the global app menu (`OC_ui.cpp:168-171`). That sends `APP_EVENT_SUSPEND`, and on close `APP_EVENT_RESUME` (`OC_apps.ino:450,484`). | | |

Encoder-only functions (not menu rows): scale select and commit (L enc + L push), scale-editor open (L long / R push on mask), the octave toggle (UP, which edits the OCTAVE setting with a toggle member), freeze (DOWN), and the delay-mechanics toggle (DOWN long).

### 5. Guards and follow-ups

- R encoder (`:893-914`):
  - Guard: `if (ASR_SETTING_MASK != setting)`. The mask cannot be changed by the encoder (and R push on that row opens the editor, so editing mode is never entered there).
  - `if (asr.change_value(setting, event.value)) asr.force_update();` `force_update()` only sets `force_update_` (`:344-346`). That flag is consumed **only on the next (delayed) TR1 trigger** in `update()` (`:467-469`), where it forces `update_scale(true, ...)`.
  - `case ASR_SETTING_CV_SOURCE:` calls `asr.update_enabled_settings(); asr_state.cursor.AdjustEnd(asr.num_enabled_settings() - 1);` then `if (!asr.get_cv_source()) asr_state.cursor.Scroll(asr_state.cursor_pos());`. That last call is a hack that clamps the cursor (and screen line) to the new end. A friendly-mode edit of CV source must do the same rebuild and `AdjustEnd`. `AdjustEnd` does not clamp `cursor_pos_` (`OC_menus.h:83-86`), so if the cursor sits beyond the new end, clamp it (e.g. `Scroll(0)`).
- `set_scale(int)` (`:113-122`) is the only correct way to change the scale. If the scale changes, it ORs bit 0 into the mask when the mask has no bits among the new scale's `num_notes` (`if (0 == (mask & ~(0xffff << scale_def.num_notes))) mask |= 0x1;`), then calls `apply_value` on MASK, then SCALE. A friendly edit of the scale must call `asr.set_scale(v)` **and** set `asr_state.left_encoder_value = v`, as `ASR_restore` does (`:818-819`). Otherwise the title keeps showing the old pending name without the "active" indicator, and the next L push reverts the scale.
- Quantizer: `update_scale(force, mask_rotate)` (`:326-342`) compares `last_scale_`/`last_mask_` with the current (CV3-rotated) values and reconfigures when they differ. It runs only inside the triggered branch of `update()` (`:456-469`). A raw `apply_value` of SCALE or MASK is therefore picked up at the next clock even without `force_update()`. All other settings are read live on each trigger (`:456-663`), so raw `apply_value` is equivalent for them.
- Nothing is applied on leaving edit mode.
- If the scale editor is open, its cached `mask_` (`OC_scale_edit.h:89`) goes stale after an external MASK write. Close the editor first (`asr_state.scale_editor.Close()`), or avoid writing while it is active.

### 6. Note masks and sub-editors

- `ASR_SETTING_MASK` (U16, min 1) is a scale note mask edited in `OC::ScaleEditor<ASR>` (`OC_scale_edit.h`), initialised with `Init(false)` (non-Meta-Q mode, `:779`).
- Opened by:
  - L long press, which commits the pending scale first (`:955-961`);
  - R push on the "mask" row (`:937-941`).

  It refuses `SCALE_NONE` (`:939`, `:959`, and `Edit()` itself, `OC_scale_edit.h:50-51`).
- Meaningful bits: `OC::Scales::GetScale(asr.get_scale(DUMMY)).num_notes` (`OC_scale_edit.h:643`; menu `:1010`). For user scales (0-3) this comes from `OC::g_user_scales[i].num_notes`, which the editor itself can change (L held + R encoder on the end marker, `OC_scale_edit.h:375-396`). For built-in scales it is the braids table value.
- Write-back: `ScaleEditor::apply_mask` calls `owner_->update_scale_mask(mask_, edit_this_scale_)`, which is `ASR::update_scale_mask` → `apply_value(ASR_SETTING_MASK, mask)` (`:357-359`). There is one object, so there is no per-channel choice.
- Note edits call `owner_->scale_changed()`, which sets `force_update_` (`:349-351`).
- Editor controls (`OC_scale_edit.h:274-433`):
  - L encoder moves the note cursor.
  - L push toggles a bit (never down to an empty mask, `:566-568`).
  - R encoder rotates the mask (`RotateMask`). On user scales, L held + R encoder changes a note's value, and on the end position it changes the length.
  - UP/DOWN invert the mask (never to zero, `:579-586`). With L held they change a note by ±1 semitone; UP on the end position resets the scale to SEMI.
  - L long opens the DAC voltage-scaling page (global `OC::DAC` state).
  - R push closes the editor.
- Bits above `num_notes` are ignored. `RotateMask` fills them with 1 (`:605`).
- The displayed mask in the list is `get_rotated_mask()` = `last_mask_` (after CV3 rotation, updated per trigger). It is not the raw setting.

### 7. Title bar (`ASR_menu`, `:971-996`)

- `TitleBar<0,4,0>`.
- Scale name: `OC::scale_names[left_encoder_value]` (long names, the *pending* scale).
- At x=1: the hold bitmap if `freeze_state()`, else `bitmap_indicator_4x8` if `asr.get_scale(DUMMY) == left_encoder_value` (pending == active).
- `"+"` at (110,2) if `poke_octave_toggle()`.
- At x=118: the hold bitmap if `get_delay_type()`, else the normal indicator.
- At x=124: the TR1 clock gate indicator from `clockState()`.
- In the list: the mask graphic on the MASK row, and the live LFSR register on the CV source row when TM is selected.

### 8. Screensaver and app events

- `ASR_screensaver` (`:1037-1085`) draws four 32-px columns of the last 5 quantised output values, scrolling as stepped lines. It is read-only.
- `ASR_handleAppEvent` (`:827-840`):
  - `RESUME` calls `cursor.set_editing(false)` and `scale_editor.Close()`.
  - `SUSPEND`, `SCREENSAVER_ON` and `SCREENSAVER_OFF` call `update_enabled_settings()` + `AdjustEnd`.
  - Shoal never sends the screensaver events (`o_c_REV.ino:151`).
- `ASR_restore` (`:815-825`) resyncs `left_encoder_value`, clears the freeze, sets the display mask and rebuilds the list.

### SettingsAccess.inc notes

- 1 channel, `asr`, `ASR_SETTING_LAST` = 25: this is correct.
- `setSettingRaw` → `apply_value` is **wrong for index 0 (Scale)**. It skips `set_scale`'s mask fix-up and leaves `asr_state.left_encoder_value` stale.
- It is also wrong for index 8 (CV source), which needs the enabled-list rebuild.
- MASK should be range-limited to the scale's `num_notes` bits (the setting range 1..65535 does not express that).

---

## Harrington 1200 (`APP_H1200.ino`)

App entry `OC_apps.ino:56` (`"Harrington 1200"`, boring name `"Triads"`).

### 1. Channels

- **One** settings object: `H1200Settings h1200_settings` (`:495-496`). Runtime state is in `H1200State h1200_state` (`:498-680`): cursor, `display_notes`, `TonnetzState`, euclidean working copies.
- There is no channel selection. `SettingsAccess.inc` (1 channel) is correct.

### 2. Settings table (`SETTINGS_DECLARE(H1200Settings, ...)`, `:432-474`; enum `:67-106`)

Octave range: the non-`BUCHLA_4U` branch, -3..3 (`:438`). Buchla is 0..7 (`:436`).

| idx | enum | name string | min | max | default | value names |
|---|---|---|---|---|---|---|
| 0 | `H1200_SETTING_ROOT_OFFSET` | `"Transpose"` | -11 | 11 | 0 | NULL |
| 1 | `H1200_SETTING_ROOT_OFFSET_CV` | `"Transpose CV"` | 0 | 4 | 0 | `OC::Strings::cv_input_names_none` = `" - ", CV1, CV2, CV3, CV4` (`OC_strings.cpp:27`) |
| 2 | `H1200_SETTING_OCTAVE` | `"Octave"` | -3 | 3 | 0 | NULL |
| 3 | `H1200_SETTING_OCTAVE_CV` | `"Octave CV"` | 0 | 4 | 0 | `cv_input_names_none` |
| 4 | `H1200_SETTING_MODE` | `"Root mode"` | 0 | 1 (`MODE_LAST-1`) | `MODE_MAJOR` (0) | `mode_names` = `Maj, Min` (`:414-416`) |
| 5 | `H1200_SETTING_INVERSION` | `"Inversion"` | -3 | 3 | 0 | NULL |
| 6 | `H1200_SETTING_INVERSION_CV` | `"Inversion CV"` | 0 | 4 | 0 | `cv_input_names_none` |
| 7 | `H1200_SETTING_PLR_TRANSFORM_PRIO` | `"PLR Priority"` | 0 | 5 | 0 | `plr_trigger_mode_names` = `P>L>R, L>R>P, R>P>L, P>R>L, R>L>P, L>P>R` (`:390-397`) |
| 8 | `H1200_SETTING_PLR_TRANSFORM_PRIO_CV` | `"PLR Prior CV"` | 0 | 4 | 0 | `cv_input_names_none` |
| 9 | `H1200_SETTING_NSH_TRANSFORM_PRIO` | `"NSH Priority"` | 0 | 5 | 0 | `nsh_trigger_mode_names` = `N>S>H, S>H>N, H>N>S, N>H>S, H>S>N, S>N>H` (`:399-406`) |
| 10 | `H1200_SETTING_NSH_TRANSFORM_PRIO_CV` | `"NSH Prior CV"` | 0 | 4 | 0 | `cv_input_names_none` |
| 11 | `H1200_SETTING_CV_SAMPLING` | `"CV sampling"` | 0 | 1 | 0 | `h1200_cv_sampling` = `Cont, Trig` (`:418-420`) |
| 12 | `H1200_SETTING_OUTPUT_MODE` | `"Output mode"` | 0 | 1 | 0 | `output_mode_names` = `Chord, Tune` (`:385-388`) |
| 13 | `H1200_SETTING_TRIGGER_DELAY` | `"Trigger delay"` | 0 | 7 | 0 | `OC::Strings::trigger_delay_times` (`off, 120us, 240us, 360us, 480us, 1ms, 2ms, 4ms`) |
| 14 | `H1200_SETTING_TRIGGER_TYPE` | `"Trigger type"` | 0 | 2 | 0 | `trigger_type_names` = `PLR, NSH, Eucl` (`:408-412`) |
| 15 | `H1200_SETTING_EUCLIDEAN_CV1_MAPPING` | `"Eucl CV1 map"` | 0 | 18 | 0 | `h1200_eucl_cv_mappings` (19): `None, Plen, Pfil, Poff, Llen, Lfil, Loff, Rlen, Rfil, Roff, Nlen, Nfil, Noff, Slen, Sfil, Soff, Hlen, Hfil, Hoff` (`:422-430`) |
| 16 | `H1200_SETTING_EUCLIDEAN_CV2_MAPPING` | `"Eucl CV2 map"` | 0 | 18 | 0 | same |
| 17 | `H1200_SETTING_EUCLIDEAN_CV3_MAPPING` | `"Eucl CV3 map"` | 0 | 18 | 0 | same |
| 18 | `H1200_SETTING_EUCLIDEAN_CV4_MAPPING` | `"Eucl CV4 map"` | 0 | 18 | 0 | same |
| 19 | `H1200_SETTING_P_EUCLIDEAN_LENGTH` | `" P EuLeng"` | 2 | 32 | 8 | NULL |
| 20 | `H1200_SETTING_P_EUCLIDEAN_FILL` | `" P EuFill"` | 0 | 32 | 0 | NULL |
| 21 | `H1200_SETTING_P_EUCLIDEAN_OFFSET` | `" P EuOffs"` | 0 | 31 | 0 | NULL |
| 22 | `H1200_SETTING_L_EUCLIDEAN_LENGTH` | `" L EuLeng"` | 2 | 32 | 8 | NULL |
| 23 | `H1200_SETTING_L_EUCLIDEAN_FILL` | `" L EuFill"` | 0 | 32 | 0 | NULL |
| 24 | `H1200_SETTING_L_EUCLIDEAN_OFFSET` | `" L EuOffs"` | 0 | 31 | 0 | NULL |
| 25 | `H1200_SETTING_R_EUCLIDEAN_LENGTH` | `" R EuLeng"` | 2 | 32 | 8 | NULL |
| 26 | `H1200_SETTING_R_EUCLIDEAN_FILL` | `" R EuFill"` | 0 | 32 | 0 | NULL |
| 27 | `H1200_SETTING_R_EUCLIDEAN_OFFSET` | `" R EuOffs"` | 0 | 31 | 0 | NULL |
| 28 | `H1200_SETTING_N_EUCLIDEAN_LENGTH` | `" N EuLeng"` | 2 | 32 | 8 | NULL |
| 29 | `H1200_SETTING_N_EUCLIDEAN_FILL` | `" N EuFill"` | 0 | 32 | 0 | NULL |
| 30 | `H1200_SETTING_N_EUCLIDEAN_OFFSET` | `" N EuOffs"` | 0 | 31 | 0 | NULL |
| 31 | `H1200_SETTING_S_EUCLIDEAN_LENGTH` | `" S EuLeng"` | 2 | 32 | 8 | NULL |
| 32 | `H1200_SETTING_S_EUCLIDEAN_FILL` | `" S EuFill"` | 0 | 32 | 0 | NULL |
| 33 | `H1200_SETTING_S_EUCLIDEAN_OFFSET` | `" S EuOffs"` | 0 | 31 | 0 | NULL |
| 34 | `H1200_SETTING_H_EUCLIDEAN_LENGTH` | `" H EuLeng"` | 2 | 32 | 8 | NULL |
| 35 | `H1200_SETTING_H_EUCLIDEAN_FILL` | `" H EuFill"` | 0 | 32 | 0 | NULL |
| 36 | `H1200_SETTING_H_EUCLIDEAN_OFFSET` | `" H EuOffs"` | 0 | 31 | 0 | NULL |

(The name strings for 19-36 begin with a space, verbatim.)

### 3. Visible settings

- `update_enabled_settings()` (`:327-377`), with `enabled_setting_at` (`:311`) and `num_enabled_settings` (`:315`).
- Indices 0-14 are always shown, in enum order (`:331-345`).
- Indices 15-36 are shown only under `switch (get_trigger_type()) { case H1200_TRIGGER_TYPE_EUCLIDEAN: ... }` (`:347-374`), also in enum order.
- So the enabled list is always the identity prefix `0..14` or `0..36`. This matters because the R encoder uses the cursor position directly as the setting index (see 5).
- No pages, no sub-editors. Every setting is reachable in the menu when Eucl is selected; indices 15-36 are hidden for PLR and NSH.

### 4. Controls (`H1200_handleButtonEvent` `:1066-1090`, `H1200_handleEncoderEvent` `:1092-1129`)

| control | action | state | setting? |
|---|---|---|---|
| L encoder | `if (change_value(H1200_SETTING_INVERSION, value)) force_update()` (`:1094-1096`) | `H1200_SETTING_INVERSION` | yes (the same setting as menu row 5) |
| R encoder, not editing | `cursor.Scroll(value)` (`:1126`) | cursor | no |
| R encoder, editing | `change_value(cursor.cursor_pos(), value)` + follow-ups (`:1098-1124`) | that setting | yes |
| L push | `display_notes = !display_notes` (`:1077-1079`): title shows note names vs numbers | `h1200_state.display_notes` | no (not saved) |
| R push | `cursor.toggle_editing()` (`:1080-1082`) | cursor | no |
| UP | `if (change_value(H1200_SETTING_OCTAVE, 1)) force_update()` (`:1069-1072`) | OCTAVE | yes |
| DOWN | `change_value(H1200_SETTING_OCTAVE, -1)` likewise (`:1073-1076`) | OCTAVE | yes |
| L long | `h1200_settings.InitDefaults(); h1200_state.manual_reset();` (`:1085-1088`): resets **all** settings to defaults and queues `ACTION_MANUAL_RESET` (a TR1 reset through the trigger delay → `tonnetz_state.reset(mode)`, `:687-690`, `:1052-1053`). It does **not** call `update_enabled_settings()`, so the list stays long if the trigger type was Eucl. | all settings + tonnetz | yes |
| DOWN long | reaches the app, but is not handled (the `else` branch only checks L) | none | |
| UP long | does nothing in Shoal (`OC_ui.cpp:159-166`) | | |
| R long | global app menu (`OC_ui.cpp:168-171`) | | |

Encoder-only functions: L encoder = inversion (also a row), UP/DOWN = octave (also a row), L push = display toggle, L long = reset all to defaults + chord reset.

### 5. Guards and follow-ups (R encoder, `:1098-1124`)

- No guard before the edit. The edit is `h1200_settings.change_value(h1200_state.cursor.cursor_pos(), event.value)`. It indexes by **cursor position, not** `enabled_setting_at(...)`. This is only correct because the enabled list is an identity prefix. A friendly-mode implementation should index by the real setting.
- When the value changed:
  1. If `setting == H1200_SETTING_TRIGGER_TYPE`: `update_enabled_settings(); cursor.AdjustEnd(...)`. This happens twice (`:1102-1105` and `:1110-1116`). The second time adds `if (get_trigger_type() != H1200_TRIGGER_TYPE_EUCLIDEAN) cursor.Scroll(cursor_pos())`, a hack that clamps the cursor to the new end.
  2. `h1200_state.force_update()` writes `ACTION_FORCE_UPDATE` to the `ui_actions` ring (`:613-615`). The ISR ORs `TRIGGER_MASK_DIRTY` (0x10) into the triggers (`:1049-1050`), but `H1200_clock` first passes them through `trigger_delays_.Process(...)` (`:684`). That only propagates bits 0-3 (`OC_trigger_delays.h`: loop over `DIGITAL_INPUT_LAST` delays). **So the DIRTY bit is dropped and `force_update()` has no effect in this version.** Settings take effect at the next real trigger, or every ISR tick when `CV sampling` = Cont (`:707`, `:1002`).
  3. If `setting == H1200_SETTING_MODE`: `h1200_settings.mode_change(true)` sets `manual_mode_change_` (`:319-321`). `H1200_clock` then resets the tonnetz to the new mode on the next TR2/TR3/TR4 trigger (`:693-699`), or on TR1. **A raw `apply_value(MODE)` does not set this flag**, so the chord would keep its old mode until a TR1 reset. Friendly mode must call `mode_change(true)` after changing MODE. (The title shows the *setting's* mode immediately, `:1134`.)
- ISR reads: all other settings are read live in `H1200_clock` (`:684-1002`). The euclidean working copies are refreshed from the settings on each trigger (`:896-913`). A raw `apply_value` is therefore equivalent for everything except MODE (flag) and TRIGGER_TYPE (enabled list and cursor clamp).
- Nothing is applied on leaving edit mode.
- Restore bug: `H1200_restore` (`:1023-1027`) calls `update_enabled_settings()` **before** `Restore()`, so after a load the list reflects the old trigger type. RESUME does not rebuild it (`:1031-1034`); only SUSPEND does (`:1035-1040`). Friendly mode should rebuild after restore.

### 6. Note masks and sub-editors

None. H1200 has no masks and no sub-editors.

### 7. Title bar (`H1200_menu`, `:1131-1154`)

- `DefaultTitleBar`.
- `note_name(outputs[0])` (the current chord root).
- `mode_names[h1200_settings.mode()]` (`Maj`/`Min` from the **setting**, not the current chord, `:1133-1134`).
- Then outputs 1-3: note names if `display_notes`, else `pretty_print` numbers (semitones) (`:1144-1154`).
- The list rows are all `DrawDefault` (`:1164`).

### 8. Screensaver and app events

- `H1200_screensaver` (`:1168-1207`) draws:
  - the last 4 transforms from `tonnetz_state.history()`, each as `+`/`-` plus the transform letter;
  - note name + octave for outputs 1-3;
  - a pitch-class circle (`visualize_pitch_classes`).

  It is read-only. One `// SHOAL:` fix at `:1185-1188` (negative-note modulo).
- `H1200_handleAppEvent` (`:1029-1042`):
  - `RESUME` calls `cursor.set_editing(false)` and `tonnetz_state.reset(h1200_settings.mode())`. This changes runtime state: the chord is reset whenever the app menu closes or the app is selected.
  - `SUSPEND`/`SCREENSAVER_*` rebuild the enabled list. Shoal never sends the screensaver events.

### SettingsAccess.inc notes

- 1 channel, `H1200_SETTING_LAST` = 37: this is correct.
- Raw `apply_value` is incomplete for MODE (needs `mode_change(true)`) and TRIGGER_TYPE (needs `update_enabled_settings()` + `AdjustEnd` + cursor clamp).
- After a restore, the enabled list is stale (see 5).

---

## Automatonnetz (`APP_AUTOMATONNETZ.ino`, `tonnetz/`)

App entry `OC_apps.ino:57` (`"Automatonnetz"`, boring name `"Vectors"`). Shares `TonnetzState` (`tonnetz/tonnetz_state.h`) and the transform enum (`tonnetz/tonnetz.h`) with H1200. It also uses H1200's `mode_names` via `extern` (`:322`).

### 1. Channels / levels

There are two levels, both `SettingsBase` objects.

- **Grid level:** `AutomatonnetzState automatonnetz_state` (`:188`, `:348-349`), `GRID_SETTING_LAST` = 7 settings.
- **Cell level:** `TransformCell cells_[GRID_CELLS]` (`:282`), with 25 cells (`GRID_DIMENSION` 5×5, `:71-72`), each a `SettingsBase<TransformCell, CELL_SETTING_LAST>` (`:107`) with 4 settings.
  - Index mapping: `index = y * 5 + x` (`util/util_grid.h` `at()`/`mutable_cell()`).
  - Access: `automatonnetz_state.cells_[i]` or `automatonnetz_state.grid.mutable_cell(i)`.
- The UI selects a cell with the **L encoder**, in both views (`ui.selected_cell`, 0..24, `:706-709`). The L push toggles between the grid view and the cell view (`ui.edit_cell`, `:685-687`).
- The cell view title reads `"CELL " + (selected_cell/5+1) + "," + (selected_cell%5+1)`, i.e. "CELL row,col", 1-based (`:515-518`).
- In the grid drawing, the selected cell is framed (`:581-582`) and the current playhead cell is inverted (`:592-595`).
- Persistence: grid settings then the 25 cells in order (`Automatonnetz_save`/`_restore` `:642-658`; size `:357-360`).

### 2a. Grid settings (`SETTINGS_DECLARE(AutomatonnetzState, ...)`, `:334-346`; enum `:156-165`)

| idx | enum | name string | min | max | default | value names |
|---|---|---|---|---|---|---|
| 0 | `GRID_SETTING_DX` | `"dx"` | 0 | `8*GRID_DIMENSION-1` (39) | 8 | NULL; displayed specially (see below). Storage I8. |
| 1 | `GRID_SETTING_DY` | `"dy"` | 0 | 39 | 4 | NULL; displayed specially |
| 2 | `GRID_SETTING_MODE` | `"Mode"` | 0 | 1 (`MODE_LAST-1`) | `MODE_MAJOR` (0) | `mode_names` (H1200's) = `Maj, Min` |
| 3 | `GRID_SETTING_OCTAVE` | `"Oct"` | -3 | 3 | 0 | NULL (Buchla: 0..7, `:339`) |
| 4 | `GRID_SETTING_TRIGGER_DELAY` | `"TrDly"` | 0 | 7 | 0 | `OC::Strings::trigger_delay_times` (`off, 120us, 240us, 360us, 480us, 1ms, 2ms, 4ms`) |
| 5 | `GRID_SETTING_OUTPUTMODE` | `"OutA"` | 0 | 3 | 0 | `outputa_mode_names` = `root, trig, arp, strm` (`:323-328`) |
| 6 | `GRID_SETTING_CLEARMODE` | `"Clr"` | 0 | 2 | 0 | `clear_mode_names` = `zero, rT, rTev` (`:330-332`) |

dx/dy encoding:

- Each step is `value/8` whole cells plus `clock_fraction[value%8]` (`:236-244`, `:76-78`). The fractions by `value%8` are: 0 → 0, 1 → 1/8, 2 → 1/7, 3 → 1/6, 4 → 1/5, 5 → 1/4, 6 → 1/3, 7 → 1/2.
- Display (`:552-561`): copy `clock_fraction_names[fraction]` (`" ", "  1/8", "  1/7", "  1/6", "  1/5", "  1/4", "  1/3", "  1/2"`), then if `integral || !fraction` replace the first char with the digit. Examples: 8 → `"1"`, 12 → `"1 1/5"`, 3 → `"  1/6"`, 0 → `"0"`.

### 2b. Cell settings (`SETTINGS_DECLARE(TransformCell, ...)`, `:149-154`; enum `:88-94`)

| idx | enum | name string | min | max | default | value names |
|---|---|---|---|---|---|---|
| 0 | `CELL_SETTING_TRANSFORM` | `"Trfm"` | `TRANSFORM_NONE` (0) | `TRANSFORM_LAST` (7) | 0 | `tonnetz::transform_names_str` = `*, P, L, R, N, S, H, @` (`tonnetz/tonnetz.h`). `*` = none; `@` (value 7) = reset the chord to the grid Mode (`:405-407`). |
| 1 | `CELL_SETTING_TRANSPOSE` | `"Offs"` | -12 | 12 | 0 | NULL |
| 2 | `CELL_SETTING_INVERSION` | `"Inv"` | -3 | 3 | 0 | NULL |
| 3 | `CELL_SETTING_EVENT` | `"Muta"` | 0 | 7 | 0 | `cell_event_masks` = `none, rT__, r_O_, rTO_, r__I, rT_I, r_OI, rTOI` (`:138-147`). This is a 3-bit mask shown as a dropdown: 1 = randomise transform, 2 = randomise offset, 4 = randomise inversion. |

Cell defaults: the app does not call `InitDefaults` per cell. `Init()` memsets them to 0 (`:193`), and `Automatonnetz_init` then calls `ClearGrid(CLEAR_MODE_RAND_TRANSFORM)` (`:353`), so a fresh app has **random transforms** in every cell.

### 3. Visible settings

- No enabled-settings list. Both menus show all their settings:
  - `ui.grid_cursor.Init(GRID_SETTING_DX, GRID_SETTING_LAST - 1)`;
  - `ui.cell_cursor.Init(CELL_SETTING_TRANSFORM, CELL_SETTING_LAST - 1)` (`:203-204`).
- The menu occupies the right half (`kMenuStartX` = 62, `:507`). The left half always shows the 5×5 grid of transform letters (`:571-595`).
- "Pages": grid view (`draw_grid_menu`, `:530-566`) vs cell view (`draw_cell_menu`, `:512-528`), switched by **L push** (`ui.edit_cell`). Each view has its own cursor and editing flag. Toggling the view does not clear the other cursor's editing state.
- There are no hidden settings.

### 4. Controls (`Automatonnetz_handleButtonEvent` `:676-702`, `Automatonnetz_handleEncoderEvent` `:704-726`)

| control | action | state | setting? |
|---|---|---|---|
| L encoder | `selected_cell += value`, constrained to `0..GRID_CELLS-1` (`:706-709`). L acceleration is disabled on RESUME (`:663`). | `ui.selected_cell` | no (UI member) |
| R encoder, cell view | editing: `cell.change_value(cell_cursor.cursor_pos(), value)` on `grid.mutable_cell(selected_cell)` (`:711-714`); else `cell_cursor.Scroll` | cell setting | yes (cell level) |
| R encoder, grid view | editing: `automatonnetz_state.change_value(grid_cursor.cursor_pos(), value)` (`:719-720`); else `grid_cursor.Scroll` | grid setting | yes (grid level) |
| L push | `ui.edit_cell = !ui.edit_cell` (`:685-687`): grid ↔ cell view | UI member | no |
| R push | `toggle_editing()` on the active view's cursor (`:688-693`) | cursor | no |
| UP | `AddUserAction(USER_ACTION_RESET)` (`:679-681`). The ISR moves the grid to the origin, resets `arp_index_` and calls `tonnetz_state.reset(mode())` (`:384-387`, `:405-407`). | runtime (grid pos, chord) | no |
| DOWN | `AddUserAction(USER_ACTION_CLOCK)` (`:682-684`): manual grid clock step (`:374-375`) | runtime | no |
| L long | `ClearGrid()` (with critical-section lock, `:212-215`) using the `Clr` setting: `zero` memsets all cells; `rT` sets random transforms; `rTev` sets every cell's `Muta` to `rT__` (`:217-232`). Then `AddUserAction(USER_ACTION_RESET)` (`:696-700`). | **all 25 cells' settings** + runtime | yes (writes cell settings, all cells) |
| DOWN long | reaches the app, but is not handled (only L long is) | none | |
| UP long | does nothing in Shoal | | |
| R long | global app menu (`OC_ui.cpp:168-171`) | | |

`Automatonnetz_rightButton()` (`:673-674`) is empty and unused.

Encoder-only functions: cell selection (L enc), view toggle (L push), reset (UP), manual clock (DOWN), clear/randomise grid (L long, which writes all cells).

### 5. Guards and follow-ups

- No guards and no follow-ups around either `change_value` (`:714`, `:720`). A raw `apply_value` is exactly equivalent to the encoder on both levels.
- Edits to cells are made without the critical section; the comment at `:47-50` calls editing while clocked "undefined behaviour".
- When the ISR picks up each setting:
  - dx/dy: on each grid move (`:389`);
  - TrDly: each ISR (`:366`);
  - Oct and OutA: on each output update (`:459-482`);
  - Mode: **only at a reset** (UP, TR1 with TR3 high `:380-381`, L long, RESUME, or a cell with `@`). Changing Mode does not re-voice the current chord until then. This matches the encoder behaviour, so it is not a friendly-mode issue.
  - Clr: only on L long.
  - Cell values: when the playhead enters that cell (`:401-416`).
- ISR-side writes: `current_cell.apply_event_masks()` (`:415`, `:127-135`) **randomly rewrites the current cell's Trfm/Offs/Inv** on each visit according to its `Muta` bits. A friendly UI must re-read cell values continuously rather than cache them.

### 6. Note masks and sub-editors

- There is no scale mask. `CELL_SETTING_EVENT` ("Muta") is a 3-bit mask, but the firmware edits it as an ordinary enumerated value 0..7 with names. It could be shown as three toggles (rT / rO / rI = bits 0x1 / 0x2 / 0x4, `:96-102`), or kept as a dropdown.
- The "cell editor" is the cell view (`draw_cell_menu`): a 4-row settings list for the cell chosen with the L encoder. It is opened and closed with L push, and it writes directly to `cells_[selected_cell]`, one cell at a time.
- The only all-cells write is `ClearGrid` (L long).

### 7. Title bar

- Grid view (`:530-543`): `TitleBar<62,1,2>` with `note_name(outputs[1..3])` (the three chord notes), then `'+'` if the **current chord's** mode is major, else `'-'`.
- Cell view (`:514-518`): `"CELL r,c"`.
- Left half (both views):
  - the 5×5 transform letters (`tonnetz::transform_names`: `* P L R N S H @`);
  - a frame on `selected_cell`;
  - an inverted rectangle on the playhead `grid.current_pos()`.

### 8. Screensaver and app events

- `Automatonnetz_screensaver` (`:615-640`) draws:
  - a pitch-class circle of outputs 1-3 at (96,32);
  - the path of the last 4 visited cells from `history()` (each a byte of `current_pos_index()` = `x*5+y`, decoded by `extract_pos`). The current cell is a filled disk, earlier cells are circles joined by lines.

  It is read-only.
- `Automatonnetz_handleAppEvent` (`:660-671`):
  - `RESUME` calls `g_ui.encoder_enable_acceleration(CONTROL_ENCODER_L, false)` and `AddUserAction(USER_ACTION_RESET)`. This changes state: the grid returns to the origin and the chord resets whenever the app menu closes or the app is selected.
  - `SUSPEND`/`SCREENSAVER_*` do nothing.
- The `ui` struct (selected cell, view, cursors) is not saved. `Init()` memsets it (`:202`).

### SettingsAccess.inc notes

- The mapping is **incomplete**: it exposes only the 7 grid settings (`GRID_SETTING_LAST`) as 1 channel; the comment at `SettingsAccess.inc:34` acknowledges this.
- The 25 × 4 cell settings (`TransformCell`, `CELL_SETTING_LAST` = 4) are not reachable. Exposing them needs either a second object kind or a 25-"channel" cell table (`cells_[ch]`), with `selected_cell` as the natural channel selector.
- Raw `apply_value` is otherwise correct for both levels: there are no follow-ups.

## Quantermain (APP_QQ.ino)

All paths are relative to `src/shoal/firmware/`. Line numbers are from the vendored file.

### 1. Channels

- There are 4 independent settings objects: `QuantizerChannel quantizer_channels[4]` (`SHOAL_STATE`, APP_QQ.ino:1189-1190). Each derives from `settings::SettingsBase<QuantizerChannel, CHANNEL_SETTING_LAST>` (:131), so it has 47 settings.
- The UI calls them A, B, C, D. The title bar prints `'A' + i` (:1256), and the selected column is highlighted with `menu::QuadTitleBar::Selected(qq_state.selected_channel)` (:1264).
- The selected channel is `qq_state.selected_channel`. It is a member of the state class `QuadQuantizer` (:1166-1185), not a setting. It is changed by:
  - L encoder: `selected_channel + event.value`, clamped 0..3 (:1361-1367);
  - L button short press: `(selected_channel + 1) & 3`, which wraps (:1442-1446).
- `QQ_init` (:1192-1201) initialises channel i with source `CV1+i` and trigger `TR1+i` (these override the table defaults). It also sets `channel_index_ = source` (:378). `channel_index_` is the channel's own input index (0..3) and is not saved.
- Save/restore: `QQ_save`/`QQ_restore` (:1207-1224) write the 4 channels back to back. Restore also calls `update_scale_mask(get_mask(), 0)` and `update_enabled_settings()` for each channel, then `cursor.AdjustEnd` using channel 0.

### 2. Settings table (`SETTINGS_DECLARE(QuantizerChannel, CHANNEL_SETTING_LAST)`, :1115-1163; enum :49-98)

`CHANNEL_SETTING_LAST` = 47. `OC::Scales::NUM_SCALES` = 102 (4 user scales + 98 braids scales, OC_scales.cpp:12), so the scale max is 101. `SCALE_SEMI` = 5 and `SCALE_NONE` = 4, shown as "Off " (OC_scales.h:20-27). `kNumDelayTimes` = 8 and `kIntSeqLen` = 128 (OC_strings.h:6,10).

String arrays used below:
- `OC::scale_names`: 102 long names (OC_scales.cpp:169), for example "User-defined 1".."User-defined 4", "Off ", "Semitone", "Ionian", ...
- `note_names_unpadded`: C, C#, D, D#, E, F, F#, G, G#, A, A#, B (OC_strings.cpp:17)
- `channel_input_sources` (:1107): "CV1","CV2","CV3","CV4","Turing","Lgstc","ByteB","IntSq"
- `aux_cv_dest` (:1111): "-","root","oct","trns","mask"
- `OC::Strings::channel_trigger_sources` (OC_strings.cpp:9): "TR1","TR2","TR3","TR4","cnt+","cnt-"
- `trigger_delay_times` (OC_strings.cpp:39): "off","120us","240us","360us","480us","1ms","2ms","4ms"
- `cv_input_names_none` (OC_strings.cpp:27): " - ","CV1","CV2","CV3","CV4"
- `trigger_input_names_none` (OC_strings.cpp:23): " - ","TR1","TR2","TR3","TR4"
- `bytebeat_equation_names` (OC_strings.cpp:43): "hope","love","life","age","clysm","monk","NERV","Trurl","Pirx","Snaut","Hari","Kris","Tichy","Bregg","Avon","Orac"
- `integer_sequence_names` (OC_strings.cpp:51): "pi","vnEck","ssdn","Dress","PNinf","Dsum","Dsum4","Dsum5","CDn2","Frcti"
- `integer_sequence_dirs` (OC_strings.cpp:55): "swing","loop"

| idx | enum | name string | min | max | default | value names | storage |
|---|---|---|---|---|---|---|---|
| 0 | CHANNEL_SETTING_SCALE | "Scale" | 0 | NUM_SCALES-1 (101) | SCALE_SEMI (5) | OC::scale_names (102) | U8 |
| 1 | CHANNEL_SETTING_ROOT | "Root" | 0 | 11 | 0 | note_names_unpadded | U8 |
| 2 | CHANNEL_SETTING_MASK | "Active notes" | 1 | 65535 | 65535 | NULL (bit mask) | U16 |
| 3 | CHANNEL_SETTING_SOURCE | "CV Source" | 0 (CV1) | 7 (IntSq) | CV1 (Init: CV1+i) | channel_input_sources | U8 |
| 4 | CHANNEL_SETTING_AUX_SOURCE_DEST | "CV aux >" | 0 | 4 | 0 | aux_cv_dest | U8 |
| 5 | CHANNEL_SETTING_TRIGGER | "Trigger source" | 0 | 5 | 5 = cnt- (Init: TR1+i) | channel_trigger_sources | U8 |
| 6 | CHANNEL_SETTING_CLKDIV | "Clock div" | 1 | 16 | 1 | NULL | U8 |
| 7 | CHANNEL_SETTING_DELAY | "Trigger delay" | 0 | 7 | 0 | trigger_delay_times | U8 |
| 8 | CHANNEL_SETTING_TRANSPOSE | "Transpose" | -5 | 7 | 0 | NULL | I8 |
| 9 | CHANNEL_SETTING_OCTAVE | "Octave" | -4 | 4 | 0 | NULL | I8 |
| 10 | CHANNEL_SETTING_FINE | "Fine" | -999 | 999 | 0 | NULL | I16 |
| 11 | CHANNEL_SETTING_TURING_LENGTH | "LFSR length" | 1 | 32 | 16 | NULL | U8 |
| 12 | CHANNEL_SETTING_TURING_PROB | "LFSR prb" | 0 | 255 | 128 | NULL | U8 |
| 13 | CHANNEL_SETTING_TURING_MODULUS | "LFSR modulus" | 2 | 121 | 24 | NULL | U8 |
| 14 | CHANNEL_SETTING_TURING_RANGE | "LFSR range" | 1 | 120 | 12 | NULL | U8 |
| 15 | CHANNEL_SETTING_TURING_PROB_CV_SOURCE | "LFSR prb CV >" | 0 | 4 | 0 | cv_input_names_none | U4 |
| 16 | CHANNEL_SETTING_TURING_MODULUS_CV_SOURCE | "LFSR mod CV >" | 0 | 4 | 0 | cv_input_names_none | U4 |
| 17 | CHANNEL_SETTING_TURING_RANGE_CV_SOURCE | "LFSR rng CV >" | 0 | 4 | 0 | cv_input_names_none | U4 |
| 18 | CHANNEL_SETTING_LOGISTIC_MAP_R | "Logistic r" | 1 | 255 | 128 | NULL | U8 |
| 19 | CHANNEL_SETTING_LOGISTIC_MAP_RANGE | "Logistic range" | 1 | 120 | 12 | NULL | U8 |
| 20 | CHANNEL_SETTING_LOGISTIC_MAP_R_CV_SOURCE | "Log r   CV >" | 0 | 4 | 0 | cv_input_names_none | U4 |
| 21 | CHANNEL_SETTING_LOGISTIC_MAP_RANGE_CV_SOURCE | "Log rng CV >" | 0 | 4 | 0 | cv_input_names_none | U4 |
| 22 | CHANNEL_SETTING_BYTEBEAT_EQUATION | "Bytebeat eqn" | 0 | 15 | 0 | bytebeat_equation_names (16) | U8 |
| 23 | CHANNEL_SETTING_BYTEBEAT_RANGE | "Bytebeat rng" | 1 | 120 | 12 | NULL | U8 |
| 24 | CHANNEL_SETTING_BYTEBEAT_P0 | "Bytebeat P0" | 1 | 255 | 8 | NULL | U8 |
| 25 | CHANNEL_SETTING_BYTEBEAT_P1 | "Bytebeat P1" | 1 | 255 | 12 | NULL | U8 |
| 26 | CHANNEL_SETTING_BYTEBEAT_P2 | "Bytebeat P2" | 1 | 255 | 14 | NULL | U8 |
| 27 | CHANNEL_SETTING_BYTEBEAT_EQUATION_CV_SOURCE | "Bb eqn CV src" | 0 | 4 | 0 | cv_input_names_none | U4 |
| 28 | CHANNEL_SETTING_BYTEBEAT_RANGE_CV_SOURCE | "Bb rng CV src" | 0 | 4 | 0 | cv_input_names_none | U4 |
| 29 | CHANNEL_SETTING_BYTEBEAT_P0_CV_SOURCE | "Bb P0  CV src" | 0 | 4 | 0 | cv_input_names_none | U4 |
| 30 | CHANNEL_SETTING_BYTEBEAT_P1_CV_SOURCE | "Bb P1  CV src" | 0 | 4 | 0 | cv_input_names_none | U4 |
| 31 | CHANNEL_SETTING_BYTEBEAT_P2_CV_SOURCE | "Bb P2  CV src" | 0 | 4 | 0 | cv_input_names_none | U4 |
| 32 | CHANNEL_SETTING_INT_SEQ_INDEX | "IntSeq" | 0 | 9 | 0 | integer_sequence_names (10) | U4 |
| 33 | CHANNEL_SETTING_INT_SEQ_MODULUS | "IntSeq modul." | 2 | 121 | 24 | NULL | U8 |
| 34 | CHANNEL_SETTING_INT_SEQ_RANGE | "IntSeq range" | 1 | 120 | 12 | NULL | U8 |
| 35 | CHANNEL_SETTING_INT_SEQ_DIRECTION | "IntSeq dir" | 0 | 1 | 1 | integer_sequence_dirs | U4 |
| 36 | CHANNEL_SETTING_INT_SEQ_BROWNIAN_PROB | "> Brownian prob" | 0 | 255 | 0 | NULL | U8 |
| 37 | CHANNEL_SETTING_INT_SEQ_LOOP_START | "IntSeq start" | 0 | kIntSeqLen-2 (126) | 0 | NULL | U8 |
| 38 | CHANNEL_SETTING_INT_SEQ_LOOP_LENGTH | "IntSeq len" | 2 | kIntSeqLen (128) | 8 | NULL | U8 |
| 39 | CHANNEL_SETTING_INT_SEQ_FRAME_SHIFT_PROB | "IntSeq FS prob" | 0 | 255 | 0 | NULL | U8 |
| 40 | CHANNEL_SETTING_INT_SEQ_FRAME_SHIFT_RANGE | "IntSeq FS rng" | 0 | 5 | 0 | NULL | U4 |
| 41 | CHANNEL_SETTING_INT_SEQ_STRIDE | "Fractal stride" | 1 | kIntSeqLen-1 (127) | 1 | NULL | U8 |
| 42 | CHANNEL_SETTING_INT_SEQ_INDEX_CV_SOURCE | "IntSeq CV   >" | 0 | 4 | 0 | cv_input_names_none | U4 |
| 43 | CHANNEL_SETTING_INT_SEQ_MODULUS_CV_SOURCE | "IntSeq mod CV" | 0 | 4 | 0 | cv_input_names_none | U4 |
| 44 | CHANNEL_SETTING_INT_SEQ_RANGE_CV_SOURCE | "IntSeq rng CV" | 0 | 4 | 0 | cv_input_names_none | U4 |
| 45 | CHANNEL_SETTING_INT_SEQ_STRIDE_CV_SOURCE | "F. stride CV >" | 0 | 4 | 0 | cv_input_names_none | U4 |
| 46 | CHANNEL_SETTING_INT_SEQ_RESET_TRIGGER | "IntSeq reset" | 0 | 4 | 0 | trigger_input_names_none | U4 |

Quirks in the table:
- `INT_SEQ_INDEX` allows 9 ("Frcti"), but `Update` clamps the index to 8 (`if (int_seq_index > 8) int_seq_index = 8;`, :625). Value 9 therefore plays the same sequence as "CDn2".
- `INT_SEQ_LOOP_START` is also written by the ISR: the frame shift calls `set_int_seq_start()`, which writes `values_[...]` directly (:316-318, :678). A friendly UI must re-read this value live. It will change without any user edit when "IntSeq FS prob" > 0.
- The `TRANSPOSE` setting range is -5..7, while the runtime clamps transpose (setting plus CV) to -12..12 (:760).

### 3. Visible settings

The app keeps its own visible-settings list: `num_enabled_settings()` (:925), `enabled_setting_at()` (:929) and `update_enabled_settings()` (:933-1008), stored in `enabled_settings_[]` and `num_enabled_settings_`. The menu draws `channel.enabled_setting_at(...)` (:1272-1273). The list order is not the enum order. It is built as follows, with these exact conditions:

1. `CHANNEL_SETTING_SCALE`: always shown.
2. `CHANNEL_SETTING_ROOT` and `CHANNEL_SETTING_MASK`: shown only `if (OC::Scales::SCALE_NONE != get_scale(DUMMY))` (:936).
3. `CHANNEL_SETTING_SOURCE`: always shown.
4. The next rows depend on `get_source()`:
   - CV1..CV4: `CHANNEL_SETTING_AUX_SOURCE_DEST` is shown only `if (get_source() != get_channel_index())` (:946), so only when the channel quantizes another channel's CV input. `channel_index_` is 0..3 for A..D.
   - TURING:
     - always: `TURING_LENGTH`;
     - only if scale != SCALE_NONE: `TURING_MODULUS` (:951);
     - always: `TURING_RANGE`, `TURING_PROB`;
     - only if scale != SCALE_NONE: `TURING_MODULUS_CV_SOURCE` (:955);
     - always: `TURING_RANGE_CV_SOURCE`, `TURING_PROB_CV_SOURCE`.
   - LOGISTIC_MAP: `LOGISTIC_MAP_R`, `_RANGE`, `_R_CV_SOURCE`, `_RANGE_CV_SOURCE`.
   - BYTEBEAT: `EQUATION`, `RANGE`, `P0`, `P1`, `P2`, then the 5 CV sources (eqn, rng, P0, P1, P2).
   - INT_SEQ: `INDEX`, `MODULUS`, `RANGE`, `DIRECTION`, `BROWNIAN_PROB`, `LOOP_START`, `LOOP_LENGTH`, `STRIDE`, `STRIDE_CV_SOURCE`, `FRAME_SHIFT_PROB`, `FRAME_SHIFT_RANGE`, `INDEX_CV_SOURCE`, `MODULUS_CV_SOURCE`, `RANGE_CV_SOURCE`, `RESET_TRIGGER` (:979-993).
5. `CHANNEL_SETTING_TRIGGER`: always shown.
6. `CLKDIV` and `DELAY`: shown only `if (get_trigger_source() < CHANNEL_TRIGGER_CONTINUOUS_UP)` (:999).
7. `OCTAVE`, `TRANSPOSE`, `FINE`: always shown, in that order (:1003-1005).

Other details:
- Indentation: `indentSetting()` (:1010-1054) indents all source sub-settings plus CLKDIV and DELAY. This only affects drawing.
- Pages: there are none. It is one scrolling list (`menu::SettingsList`, :1269), and the R encoder scrolls it when not editing (:1407).
- Never shown: every setting can appear under some condition. Settings for the sources that are not selected are hidden but keep their values.
- `ScreenCursor::AdjustEnd` (OC_menus.h:83-86) does not clamp `cursor_pos_`. After the list shrinks, the cursor can point past `num_enabled_settings_` (stale array entries) until the next scroll. This is an upstream quirk.

### 4. Controls

Dispatch is in OC_ui.cpp:117-187:
- A short press goes to `app->HandleButtonEvent` (:155).
- UP long press does nothing in Shoal (:159-166; upstream it was the screensaver).
- R long press opens the global app menu, `AppMenuOpen()` (:168-170). It never reaches the app.
- Other long presses (L, DOWN) go to the app (:173).

QQ handlers:
- `QQ_handleButtonEvent` (:1324-1353):
  - It first runs `if (LONG_PRESS && CONTROL_BUTTON_DOWN) QQ_downButtonLong();` (:1326-1327). This runs before the scale-editor check, so it also fires while the editor is open.
  - If the scale editor is active, the event goes to the editor and the handler returns (:1329-1332).
  - Otherwise short presses are dispatched to the button functions. For a non-press event, `if (CONTROL_BUTTON_L == event.control) QQ_leftButtonLong();` (:1349-1352).
- `QQ_handleEncoderEvent` (:1355-1410): while the editor is active it takes all encoder events (:1356-1359).

| Control | Function | Effect | State changed |
|---|---|---|---|
| L encoder | :1361-1367 | Select a channel, `selected_channel + value` clamped 0..3, then `cursor.AdjustEnd(...)`. The edit flag and cursor position are kept. | `qq_state.selected_channel` (member, not a setting) |
| R encoder, not editing | :1407 | `cursor.Scroll(value)` | cursor (member) |
| R encoder, editing | :1368-1405 | `change_value(setting, delta)` on the row under the cursor, except MASK (see §5) | the setting in the table |
| L push | `QQ_leftButton` :1442-1446 | Next channel, wraps `& 3`; AdjustEnd | `selected_channel` |
| R push | `QQ_rightButton` :1426-1440 | On the MASK row: `scale_editor.Edit(&selected, scale)` if scale != SCALE_NONE. On any other row: `cursor.toggle_editing()`. | editor state / cursor edit flag (members) |
| UP | `QQ_topButton` :1412-1417 | `change_value(CHANNEL_SETTING_OCTAVE, 1)`, then `force_update()` if it changed | setting 9 OCTAVE (encoder-only shortcut for a menu row) |
| DOWN | `QQ_lowerButton` :1419-1424 | `change_value(CHANNEL_SETTING_OCTAVE, -1)`, then `force_update()` | setting 9 OCTAVE |
| L long | `QQ_leftButtonLong` :1448-1458 | Copy the selected channel's root and scale to the other 3 channels: `apply_value(CHANNEL_SETTING_ROOT, root)` and `set_scale(scale)`. `set_scale` (:138-147) first ORs bit 0 into the mask if no bit within the new scale's `num_notes` is set, then applies MASK and SCALE. | settings 0, 1, 2 of the other channels |
| DOWN long | `QQ_downButtonLong` :1460-1464 | `update_scale_mask(0xFFFF, 0)` on the selected channel only. This resets the mask to all notes. | setting 2 MASK, plus `last_mask_` and `force_update_` |
| UP long | OC_ui.cpp:159-166 | Nothing in Shoal | none |
| R long | OC_ui.cpp:168 | Global app menu | n/a |

Encoder-only functions (things that are not a menu-row edit):
- Channel select (L encoder or L push).
- Octave +/- (UP/DOWN). This is the same setting as the Octave row.
- Copy scale and root to all channels (L long).
- Reset mask (DOWN long).
- Open the scale editor (R push on the "Active notes" row).

`QQ_leftButtonLong` does **not** call `update_enabled_settings()` or `force_update()` on the other channels. If the copy changes another channel's scale to or from SCALE_NONE, that channel's visible list stays stale until it is next rebuilt: by an R-encoder edit of Scale, Trigger or CV Source, or by a restore. L encoder and L push call only `AdjustEnd`, not a rebuild. The quantizer still picks up the change (see §5).

### 5. Guards and follow-ups on an R-encoder edit (:1368-1405)

Before the change:
- Masks are skipped: `if (CHANNEL_SETTING_MASK != setting)` (:1372). The mask is never changed by the encoder.
- On TRIGGER: `if (selected.get_trigger_source() == CHANNEL_TRIGGER_TR4 && selected.get_source() > CHANNEL_SOURCE_CV4 && event.value > 0) event_value = 0x0;` (:1379-1380). With an internal source (Turing, Lgstc, ByteB, IntSq), the trigger cannot step from TR4 up to cnt+/cnt-.
- On SOURCE: `if (selected.get_source() == CHANNEL_SOURCE_CV4 && selected.get_trigger_source() > CHANNEL_TRIGGER_TR4 && event.value > 0) event_value = 0x0;` (:1384-1385). With a continuous trigger, the source cannot step from CV4 into the internal sources.
- These guards only test the boundary value with the raw delta. A delta > 1 (encoder acceleration, or a direct set) from TR3 or CV3 would bypass them. A friendly mode should enforce the invariant directly: internal source ⇒ trigger ≤ TR4, and trigger ≥ cnt+ ⇒ source ≤ CV4.
- The menu reflects the same rule visually only: `DrawValueMax(..., CHANNEL_TRIGGER_TR4)` limits the edit arrow (:1293-1294, :1313-1314; OC_menus.h:382-395).

The change itself: `if (selected.change_value(setting, event_value)) selected.force_update();` (:1392-1393).

After the change:
- For `CHANNEL_SETTING_SCALE`, `CHANNEL_SETTING_TRIGGER` and `CHANNEL_SETTING_SOURCE`: `selected.update_enabled_settings(); qq_state.cursor.AdjustEnd(selected.num_enabled_settings() - 1);` (:1395-1404).
- Note that editing Scale from the menu does **not** run `set_scale()`'s mask fix-up. Only L long uses `set_scale`.

To match the encoder exactly, a friendly-mode edit is: apply the guards, then `change_value`/`apply_value`, then `force_update()` if the value changed, then rebuild the list (and the cursor end) for SCALE, TRIGGER or SOURCE.

Will a raw `apply_value` be picked up by the ISR? Yes, for every setting except the visible list:
- `Update` (:414-855) reads every setting live each tick.
- Scale and mask: on each trigger, or every tick in continuous mode, `update_scale(force_update_, schedule_mask_rotate_)` (:445-446, :1087-1104) compares `last_scale_ != scale || last_mask_ != mask` and reconfigures the quantizer. A raw scale or mask change is therefore applied at the next clock, even without `force_update`.
- `AUX_SOURCE_DEST` changes are detected via `prev_destination_` and `clear_dest()` (:728-730).
- What raw `apply_value` misses:
  - `update_enabled_settings()` (and `AdjustEnd`) after SCALE, SOURCE or TRIGGER;
  - for MASK, `last_mask_`, which is what the menu's mask display draws (`get_rotated_scale_mask()` → `last_mask_`, :176-178, :1288). With no clock, the drawn mask is stale until the next trigger. `update_scale_mask()` (:866-870) sets it immediately.

  Prefer `update_scale_mask(mask, 0)` for mask writes.

### 6. Note masks and sub-editors

- `CHANNEL_SETTING_MASK` (idx 2, U16) is the only bit mask. It is edited with `OC::ScaleEditor<QuantizerChannel>`, `qq_state.scale_editor`, initialised with `Init(false)` (:1171), so the editor runs in plain scale-edit mode, not Meta-Q.
- It is opened by R push on the "Active notes" row when the scale != SCALE_NONE (:1429-1433 → `Edit(&selected, scale)`, OC_scale_edit.h:49-67).
- Meaningful bits: `OC::Scales::GetScale(scale).num_notes`:
  - the menu uses it for drawing (:1288);
  - the editor sets `num_notes_ = scale_->num_notes` (OC_scale_edit.h:643);
  - the quantizer and `RotateMask` use it too (:1094).
  - User scales 0-3 live in global `OC::g_user_scales[]`, not in the channel. Their `num_notes` (4..16) is changed by the editor. "Off" has 0 notes.
- Write-back: `ScaleEditor::apply_mask()` → `owner_->update_scale_mask(mask_, edit_this_scale_)` (OC_scale_edit.h:104-110). This calls `QuantizerChannel::update_scale_mask` (:866-870): `apply_value(CHANNEL_SETTING_MASK, mask); last_mask_ = mask; force_update_ = true;`. It writes **one channel**, live, on every change. Nothing is deferred to closing.
- Inside the editor:
  - R encoder rotates the mask (`RotateMask`, which fills the unused upper bits with 1s);
  - L push toggles the note under the cursor (refusing to clear the last active note);
  - UP/DOWN invert the mask (`!SEQ_MODE`, :476-477 / :514-515), refusing an all-zero result;
  - L encoder moves the cursor.
  - On user scales only: L held + R encoder or UP/DOWN edits note pitch, written to the global user scale and notified with `owner_->scale_changed()` → `force_update_`. With the cursor on the end marker, R encoder changes the scale length (4..16).
  - L long goes to the voltage-scaling page, which is global DAC scaling. Any other long press leaves that page.
  - R push closes the editor (:287-292).
- QQ quirk: DOWN long while the editor is open resets the channel mask (:1326-1327), but the editor's cached `mask_` is not refreshed. The next editor action re-applies the old cached mask plus that edit, undoing the reset.

### 7. Title bar and live indicators

From `QQ_menu` (:1250-1264), the `menu::QuadTitleBar` has 4 columns. Each shows:
- the channel letter `A`..`D`;
- the octave if nonzero, `pretty_print(octave)`, so the sign is shown;
- the gate/trigger indicator `DrawGateIndicator(i, channel.getTriggerState())`.

The selected channel column is highlighted. There is no scale name or root in the title.

In the list:
- the Scale row prints `OC::scale_names[value]` with a custom layout (:1278-1286);
- the mask row draws the live rotated mask, `get_rotated_scale_mask()`, as boxes (:1287-1290);
- when the source is Turing, the CV Source row shows the live shift register bits instead of the source name (:1300-1307).

### 8. Screensaver

`QQ_screensaver` (:1537-1553) calls `RenderScreensaver(x)` for each channel at x = 0/32/64/96 (:1478-1535). Each channel draws:
- a gate indicator;
- a source view: Turing shift register bits, logistic-map register, bytebeat register, the int-seq `k` value, or, for CV sources, a bar for that CV input;
- semitone tick marks;
- the 5-step scrolling pitch history;
- an octave marker.

`QQ_handleAppEvent` (:1226-1237):
- `APP_EVENT_RESUME`: `cursor.set_editing(false); scale_editor.Close();`. This leaves edit mode and closes the editor.
- `SUSPEND`, `SCREENSAVER_ON` and `SCREENSAVER_OFF` do nothing.

---

## Meta-Q (APP_DQ.ino)

### 1. Channels

- There are 2 independent settings objects: `DQ_QuantizerChannel dq_quantizer_channels[NUMCHANNELS]`, with `NUMCHANNELS = 2` (:45, :1150-1151). Each derives from `SettingsBase<DQ_QuantizerChannel, DQ_CHANNEL_SETTING_LAST>` (:137), so it has 31 settings.
- The UI calls them A and B (title bar `'A' + i`, :1230). Channel A outputs pitch on DAC A with aux on C; B outputs on DAC B with aux on D (`DQ_isr`, :1202-1208).
- The selected channel is `dq_state.selected_channel`, a member of `DualQuantizer` (:1127-1146). It is changed by:
  - L encoder: clamped 0..1, followed by `update_enabled_settings()`, `AdjustEnd` and `cursor.Scroll(0)` (:1351-1361);
  - L push: `(selected_channel + 1) & 1u` (:1456-1460).
- Init (:1153-1161): channel i gets source `CV1 + 2*i` and trigger `TR1 + 2*i`, so A = CV1/TR1 and B = CV3/TR3.

**Scale slots.** Each channel has `NUM_SCALE_SLOTS = 4` (:46) scale "slots". Each slot is a group of **separate flat settings**:

| slot | scale | root | mask | transpose |
|---|---|---|---|---|
| 1 | SCALE1 (0) | ROOT1 (4) | MASK1 (9) | TRANSPOSE1 (17) |
| 2 | SCALE2 (1) | ROOT2 (5) | MASK2 (10) | TRANSPOSE2 (18) |
| 3 | SCALE3 (2) | ROOT3 (6) | MASK3 (11) | TRANSPOSE3 (19) |
| 4 | SCALE4 (3) | ROOT4 (7) | MASK4 (12) | TRANSPOSE4 (20) |

- The accessors `get_scale/get_root/get_mask/get_transpose(slot)` switch on the slot (:140-298).
- The **edited/selected** slot is the setting `DQ_CHANNEL_SETTING_SCALE_SEQ` (idx 8, "scale #"), read by `get_scale_select()` (:160-162).
- The **playing** slot is runtime state, not a setting:
  - `active_scale_slot_` is driven by `SCALE_SEQ`, the scale sequencer (`SEQ_MODE`, advanced by TR2 for A and TR4 for B, :447-467), and the aux CV "scl#" offset.
  - `display_scale_slot_` (:489, :505, :534) is what the title bar and the mask row show.
- Octave, source, trigger and the rest are per channel, not per slot.

### 2. Settings table (`SETTINGS_DECLARE(DQ_QuantizerChannel, DQ_CHANNEL_SETTING_LAST)`, :1092-1124; enum :48-81)

`DQ_CHANNEL_SETTING_LAST` = 31. `PULSEW_MAX` = 255 is defined in APP_A_SEQ.ino:47.

String arrays used below:
- `dq_seq_scales` (:1072): "s#1","s#2","s#3","s#4"
- `dq_seq_modes` (:1076): "-","TR+1","TR+2","TR+3"
- `dq_aux_outputs` (:1080): "gate","copy","asr"
- `dq_aux_cv_dest` (:1084): "-","scl#","root","oct","trns","mask"
- `dq_tm_trig_out` (:1088): "echo","lsb","chng"
- `OC::Strings::cv_input_names` (OC_strings.cpp:25): "CV1","CV2","CV3","CV4". This array has **only 4 entries**.
- `OC::Strings::TM_aux_cv_destinations` (OC_strings.cpp:68): "-","rng","len","prb"
- `OC::scale_names`, `note_names_unpadded`, `channel_trigger_sources` and `trigger_delay_times`: as in QQ.

| idx | enum | name string | min | max | default | value names | storage |
|---|---|---|---|---|---|---|---|
| 0 | DQ_CHANNEL_SETTING_SCALE1 | "scale" | 0 | 101 | SCALE_SEMI (5) | OC::scale_names | U8 |
| 1 | DQ_CHANNEL_SETTING_SCALE2 | "scale" | 0 | 101 | 5 | OC::scale_names | U8 |
| 2 | DQ_CHANNEL_SETTING_SCALE3 | "scale" | 0 | 101 | 5 | OC::scale_names | U8 |
| 3 | DQ_CHANNEL_SETTING_SCALE4 | "scale" | 0 | 101 | 5 | OC::scale_names | U8 |
| 4 | DQ_CHANNEL_SETTING_ROOT1 | "root #1" | 0 | 11 | 0 | note_names_unpadded | U8 |
| 5 | DQ_CHANNEL_SETTING_ROOT2 | "root #2" | 0 | 11 | 0 | note_names_unpadded | U8 |
| 6 | DQ_CHANNEL_SETTING_ROOT3 | "root #3" | 0 | 11 | 0 | note_names_unpadded | U8 |
| 7 | DQ_CHANNEL_SETTING_ROOT4 | "root #4" | 0 | 11 | 0 | note_names_unpadded | U8 |
| 8 | DQ_CHANNEL_SETTING_SCALE_SEQ | "scale #" | 0 | 3 | 0 | dq_seq_scales | U8 |
| 9 | DQ_CHANNEL_SETTING_MASK1 | "--> edit" | 1 | 65535 | 65535 | NULL (bit mask) | U16 |
| 10 | DQ_CHANNEL_SETTING_MASK2 | "--> edit" | 1 | 65535 | 65535 | NULL | U16 |
| 11 | DQ_CHANNEL_SETTING_MASK3 | "--> edit" | 1 | 65535 | 65535 | NULL | U16 |
| 12 | DQ_CHANNEL_SETTING_MASK4 | "--> edit" | 1 | 65535 | 65535 | NULL | U16 |
| 13 | DQ_CHANNEL_SETTING_SEQ_MODE | "seq_mode" | 0 | 3 | 0 | dq_seq_modes | U4 |
| 14 | DQ_CHANNEL_SETTING_SOURCE | "CV source" | 0 (CV1) | 4 (Turing) | CV1 (Init: CV1/CV3) | cv_input_names (**4 entries; value 4 = Turing has no string**) | U4 |
| 15 | DQ_CHANNEL_SETTING_TRIGGER | "trigger source" | 0 | 5 | 5 = cnt- (Init: TR1/TR3) | channel_trigger_sources | U8 |
| 16 | DQ_CHANNEL_SETTING_DELAY | "--> latency" | 0 | 7 | 0 | trigger_delay_times | U8 |
| 17 | DQ_CHANNEL_SETTING_TRANSPOSE1 | "transpose #1" | -5 | 7 | 0 | NULL | I8 |
| 18 | DQ_CHANNEL_SETTING_TRANSPOSE2 | "transpose #2" | -5 | 7 | 0 | NULL | I8 |
| 19 | DQ_CHANNEL_SETTING_TRANSPOSE3 | "transpose #3" | -5 | 7 | 0 | NULL | I8 |
| 20 | DQ_CHANNEL_SETTING_TRANSPOSE4 | "transpose #4" | -5 | 7 | 0 | NULL | I8 |
| 21 | DQ_CHANNEL_SETTING_OCTAVE | "octave" | -4 | 4 | 0 | NULL | I8 |
| 22 | DQ_CHANNEL_SETTING_AUX_OUTPUT | "aux.output" | 0 | 2 | 0 (gate) | dq_aux_outputs | U8 |
| 23 | DQ_CHANNEL_SETTING_PULSEWIDTH | "--> pw" | 0 | PULSEW_MAX (255) | 25 | NULL; drawn by `Draw_PW_Value`: 0 → "echo", 255 → "50%", otherwise the number (ms) (OC_menus.h:360-380) | U8 |
| 24 | DQ_CHANNEL_SETTING_AUX_OCTAVE | "--> aux +/-" | -5 | 5 | 0 | NULL | I8 |
| 25 | DQ_CHANNEL_SETTING_AUX_CV_DEST | "CV aux." | 0 | 5 | 0 | dq_aux_cv_dest | U8 |
| 26 | DQ_CHANNEL_SETTING_TURING_LENGTH | " > LFSR length" | 1 | 32 | 16 | NULL | U8 |
| 27 | DQ_CHANNEL_SETTING_TURING_PROB | " > LFSR p" | 0 | 255 | 128 | NULL | U8 |
| 28 | DQ_CHANNEL_SETTING_TURING_CV_SOURCE | " > LFSR CV" | 0 | 3 | 0 | TM_aux_cv_destinations | U8 |
| 29 | DQ_CHANNEL_SETTING_TURING_RANGE | " > LFSR range" | 1 | 120 | 15 | NULL | U8 |
| 30 | DQ_CHANNEL_SETTING_TURING_TRIG_OUT | " > LFSR TRIG" | 0 | 2 | 0 | dq_tm_trig_out | U8 |

Notes on the table:
- The 4 scale settings all have the same name "scale", and the 4 masks all have the same name "--> edit". A friendly UI needs its own labels, such as "scale #n".
- The menu never prints `cv_input_names[4]`. When the source is Turing, the CV Source row draws the shift register with `DrawNoValue` instead (:1286-1293). A generic dropdown that indexes `value_names[value]` would read past the end of the array. Supply a fifth label, for example "Turing".
- Hard-wired I/O that the menu does not show:
  - aux CV ("CV aux.") reads CV2 for A and CV4 for B (`channel_id`, :483);
  - the Turing "LFSR CV" modulation reads CV1 or CV3 (`cv_source = channel_id - 1`, :543-549);
  - the scale-sequence advance clock is TR2 for A and TR4 for B (:449);
  - the continuous-mode +/- octave offset gate is TR1 or TR3 (:676).
- `TURING_CV_SOURCE` == 1 ("rng") falls through into case 2: there is no `break`, so it also modulates length (:571-577). This is an upstream quirk.

### 3. Visible settings

`num_enabled_settings()`, `enabled_setting_at()` and `update_enabled_settings()` are at :883-1001. Below, S = `get_scale_select()` (setting 8). The list is built as:

1. `SCALE<S>`: always shown (:894-910).
2. If `OC::Scales::SCALE_NONE != get_scale(get_scale_select())` (:913):
   - `MASK<S>`
   - `DQ_CHANNEL_SETTING_SEQ_MODE`
   - `DQ_CHANNEL_SETTING_SCALE_SEQ`
   - `ROOT<S>`, `TRANSPOSE<S>` (:915-956)
3. `DQ_CHANNEL_SETTING_OCTAVE` and `DQ_CHANNEL_SETTING_SOURCE`: always shown.
4. If `get_source() == DQ_CHANNEL_SOURCE_TURING`: `TURING_RANGE`, `TURING_LENGTH`, `TURING_PROB`, `TURING_CV_SOURCE`, `TURING_TRIG_OUT` (:964-975).
5. `DQ_CHANNEL_SETTING_AUX_CV_DEST` and `DQ_CHANNEL_SETTING_TRIGGER`: always shown.
6. `DQ_CHANNEL_SETTING_DELAY`: shown only `if (get_trigger_source() < DQ_CHANNEL_TRIGGER_CONTINUOUS_UP)` (:980).
7. `DQ_CHANNEL_SETTING_AUX_OUTPUT`: always shown.
8. Then, by aux output: `DQ_GATE` → `PULSEWIDTH`; `DQ_COPY` or `DQ_ASR` → `AUX_OCTAVE` (:985-998).

Pages: there are none. It is one scrolling list. The **scale slot acts like a page selector**: changing "scale #" swaps which SCALE, MASK, ROOT and TRANSPOSE settings appear.

Hidden or rarely shown:
- The 3 non-selected slots' SCALE, ROOT, MASK and TRANSPOSE are never shown in the menu at the same time. They are reached by changing "scale #". In the scale editor, Meta-Q mode can also cycle slots and edit any slot's scale, root and transpose (§6).
- When the selected slot's scale is "Off " (SCALE_NONE), the "scale #" row disappears too. To reach another slot, the user must first set this slot's scale back to something other than Off.

### 4. Controls

`DQ_handleButtonEvent` (:1314-1343) and `DQ_handleEncoderEvent` (:1345-1421) have the same structure as QQ. DOWN long is handled before the editor check (:1316-1317). The editor takes all events while it is active (:1319-1322, :1346-1349).

| Control | Function | Effect | State changed |
|---|---|---|---|
| L encoder | :1351-1361 | Select channel 0..1 (clamped), then `update_enabled_settings()`, `AdjustEnd`, `cursor.Scroll(0)`, which clamps the cursor. The edit flag is kept. | `dq_state.selected_channel` (member) |
| R encoder, not editing | :1418 | `cursor.Scroll(value)` | cursor |
| R encoder, editing | :1363-1416 | `change_value` on the row under the cursor (see §5) | setting |
| L push | `DQ_leftButton` :1456-1460 | Toggle A/B; AdjustEnd. There is no list rebuild and no Scroll(0). | `selected_channel` |
| R push | `DQ_rightButton` :1437-1454 | On a MASK1..4 row: `scale_editor.Edit(&selected, get_scale(get_scale_select()))` if the scale is not NONE. On any other row: toggle editing. | editor / edit flag |
| UP | `DQ_topButton` :1423-1428 | `change_value(DQ_CHANNEL_SETTING_OCTAVE, 1)`, then `force_update()` | setting 21 |
| DOWN | `DQ_lowerButton` :1430-1435 | `change_value(DQ_CHANNEL_SETTING_OCTAVE, -1)`, then `force_update()` | setting 21 |
| L long | `DQ_leftButtonLong` :1462-1476 | Read the selected channel's current slot S (scale, mask, root, transpose). Write it to **all 4 slots of both channels**: `set_scale_at_slot(scale, mask, root, transpose, i)` for i 0..3, j 0..1. | settings 0-7, 9-12, 17-20 on both channels |
| DOWN long | `DQ_downButtonLong` :1478-1483 | `set_scale_at_slot(get_scale(S), 0xFFFF, get_root(S), get_transpose(S), S)` on the selected channel. This resets slot S's mask to all notes. It also runs when the editor is open. | MASK<S> |
| UP long | OC_ui.cpp:159-166 | Nothing | n/a |
| R long | OC_ui.cpp:168 | Global app menu | n/a |

How `set_scale_at_slot` (:176-214) behaves:
- It only writes if `scale != get_scale(slot) || mask != get_mask(slot) || root != root_last_ || transpose != transpose_last_`. Note that `root_last_` and `transpose_last_` are single per-channel members, not per slot.
- It ORs bit 0 into the mask if no bit within the scale's `num_notes` is set.
- It then applies MASK, SCALE, ROOT and TRANSPOSE for that slot.
- It calls neither `update_enabled_settings()` nor `force_update()`.

Neither L long nor DOWN long rebuilds the visible list.

Encoder-only functions:
- Channel select.
- Octave +/-, which is the same as the octave row.
- Copy the current slot to all slots of both channels (L long).
- Reset the mask of the current slot (DOWN long).
- Open the scale editor.
- Inside the editor: cycle slots, change a slot's scale, edit a slot's root and transpose (§6).

### 5. Guards and follow-ups on an R-encoder edit (:1363-1416)

**Mask guard bug (upstream).** The mask guard is `if (DQ_CHANNEL_SETTING_MASK1 != setting || DQ_CHANNEL_SETTING_MASK2 != setting || DQ_CHANNEL_SETTING_MASK3 != setting || DQ_CHANNEL_SETTING_MASK4 != setting)` (:1370). It is **always true**: it uses `||`, where `&&` was intended. A mask row in edit mode would get a raw `change_value(MASKn, ±delta)`. Normally R push on a mask row opens the editor instead of edit mode, but edit mode carries across a channel switch or a list change. Example: channel A has scale Off, the user is editing row 1 (octave), then L-pushes to B, where row 1 is MASK<S>. The R encoder then nudges the mask integer. Do not reproduce this.

Before the change:
- On TRIGGER: `if (selected.get_trigger_source() == DQ_CHANNEL_TRIGGER_TR4 && selected.get_source() > DQ_CHANNEL_SOURCE_CV4 && event_value > 0) event_value = 0x0;` (:1379-1380).
- On SOURCE: `if (selected.get_source() == DQ_CHANNEL_SOURCE_CV4 && selected.get_trigger_source() > DQ_CHANNEL_TRIGGER_TR4 && event_value > 0) event_value = 0x0;` (:1385-1386).
- The invariant is: Turing source ⇒ trigger ≤ TR4, and continuous trigger ⇒ source ≤ CV4. As in QQ, the guards only test the boundary value with the raw delta.

The change itself: `if (selected.change_value(setting, event_value)) selected.force_update();` (:1393-1394).

After the change:
- For SCALE1-4, TRIGGER, SOURCE and AUX_OUTPUT: `update_enabled_settings(); cursor.AdjustEnd(...)` (:1397-1406).
- For SCALE_SEQ and SEQ_MODE: `update_enabled_settings(); cursor.AdjustEnd(...); selected.reset_scale();` (:1407-1412). `reset_scale()` (:354-356) sets `scale_reset_`. At the next ISR tick (:438-445) that resets `scale_sequence_cnt_ = 0`, `scale_advance_state_ = 1` and `active_scale_slot_ = prev_scale_slot_ = display_scale_slot_ = get_scale_select()`.
- A menu edit of SCALE<S> does not apply `set_scale_at_slot`'s mask fix-up, and it can select "Off " (4), which the editor skips.

Will a raw `apply_value` be picked up by the ISR?
- SCALEn and MASKn: yes. On each update, `update_scale(force_update_, display_scale_slot_, schedule_mask_rotate_)` (:540, :1052-1069) compares `last_scale_[slot]` and `last_mask_[slot]` for the playing slot. It reconfigures the quantizer at the next trigger, or every tick in continuous mode.
- ROOTn, TRANSPOSEn, OCTAVE, SOURCE, TRIGGER, AUX_*, TURING_*, PULSEWIDTH and DELAY: yes, they are read live each update. `AUX_CV_DEST` changes trigger `clear_dest()` (:485-487).
- SCALE_SEQ: in sequence mode `-` (0), the ISR notices `prev_scale_slot_ != get_scale_select()` and switches (:454-457). In sequence mode != 0 it does **not**: without `reset_scale()` the active slot and the sequence counter are not reset. Also, `prev_scale_slot_` is reassigned at :489 to `active + prev_scale_cv_`, so detection is fragile.
- SEQ_MODE: without `reset_scale()` the sequence counter is not reset.
- A friendly-mode write must replicate:
  - `force_update()`;
  - `update_enabled_settings()` after SCALE1-4, SCALE_SEQ, SEQ_MODE, SOURCE, TRIGGER or AUX_OUTPUT;
  - `reset_scale()` after SCALE_SEQ or SEQ_MODE.
- For masks, prefer `update_scale_mask(mask, slot)` (:848-872). It also sets `last_mask_[slot]`, which the menu mask row draws.

### 6. Note masks and sub-editors

- MASK1..MASK4 (idx 9-12, U16) are edited by `OC::ScaleEditor<DQ_QuantizerChannel>` `dq_state.scale_editor`, initialised with `Init(true)` (:1132), which is **Meta-Q / SEQ_MODE**.
- It is opened by R push on the mask row (:1440-1447), only when the selected slot's scale is not NONE. `BeginEditing(true)` sets `edit_this_scale_ = owner_->get_scale_select()` and `mask_ = get_scale_mask(edit_this_scale_)` (OC_scale_edit.h:645-649).
- Meaningful bits: `OC::Scales::GetScale(get_scale(slot)).num_notes`. The editor uses `num_notes_ = scale_->num_notes` (OC_scale_edit.h:643).
  - The **menu mask row** instead draws `get_rotated_mask(get_display_scale())` with `GetScale(get_scale(get_display_scale())).num_notes` (:1273). That is the playing slot, which can differ from the selected slot S when the sequencer or aux CV moves it.
- Write-back: `apply_mask` → `update_scale_mask(mask, edit_this_scale_)` (:848-872). It applies MASK<slot>, sets `last_mask_[slot]` and sets `force_update_`. It writes **one slot of one channel**, live.
- Editing actions in Meta-Q mode:
  - R encoder rotates the mask;
  - L push toggles a note;
  - L encoder moves the cursor;
  - UP/DOWN do **not** invert the mask here. They cycle `edit_this_scale_` through slots 0..3, but only when `ticks_ > 250` (OC_scale_edit.h:480-503, 518-541). In Shoal, `ticks_` is advanced by `DQ_menuRedrawn()` (:1214-1217), not by drawing.
  - L encoder while UP is held (`g_ui.read_immediate(CONTROL_BUTTON_UP)`) changes the slot's scale and skips "Off ". It writes via `set_scale_at_slot` (OC_scale_edit.h:324-355).
  - L push while UP is held goes to the root/transpose page. There, R encoder edits ROOT<slot> (0..11) or TRANSPOSE<slot>. The editor clamps transpose to -12..12, but the setting clamps it to -5..7. Both are written via `set_scale_at_slot` (OC_scale_edit.h:404-417). L encoder switches between root and transpose; L push returns to the mask page.
  - L held + R encoder or UP/DOWN edits user-scale notes, and the end marker sets the length, as in QQ. These write the global `g_user_scales`.
  - L long opens the voltage-scaling page.
  - R push closes the editor.
- The title shows `scale_id` ">#n" or " #n", marking whether the edited slot is the selected one (OC_scale_edit.h:144-150).
- Edits made in the editor never call `update_enabled_settings()`. This is harmless because the editor cannot select Off.

### 7. Title bar and live indicators

From `DQ_menu` (:1219-1243), the `menu::DualTitleBar` has 2 columns. Each shows:
- the gate indicator;
- the channel letter `A`/`B`;
- `#` followed by `get_display_scale() + 1`, the **playing** slot, which changes live with the sequencer or CV;
- a root note name: `display_root_` (CV-modulated) if `get_aux_cv_dest() == DQ_DEST_ROOT`, otherwise `get_root(get_display_scale())`;
- the octave if nonzero.

The selected column is highlighted.

In the list:
- the Scale row prints only the scale name, with no label (:1257-1268);
- the mask row draws the live mask of the playing slot (:1269-1275);
- when the source is Turing, the CV Source row draws the shift register using `turing_display_length_` (:1286-1293);
- PULSEWIDTH uses `Draw_PW_Value` (:1301-1302).

### 8. Screensaver

`DQ_screensaver` (:1545-1559) calls `RenderScreensaver` at x = 0 and 64 (:1497-1543). Each channel draws:
- a gate indicator;
- the Turing shift register (16 bits, display length), or a bar for the source CV input;
- semitone ticks;
- the 5-step pitch history;
- an octave marker.

`DQ_handleAppEvent` (:1189-1200):
- `RESUME`: `cursor.set_editing(false); scale_editor.Close();`.
- `SUSPEND`, `SCREENSAVER_ON` and `SCREENSAVER_OFF` do nothing.

While the Meta-Q editor is open, `_preemptScreensaver(true)` is set (OC_scale_edit.h:648); `Close` clears it.

---

### SettingsAccess.inc check (Quantermain and Meta-Q entries)

- QQ, `Entry<QuantizerChannel, CHANNEL_SETTING_LAST, 4, &qqObj>` (SettingsAccess.inc:51): the counts are correct (4 × 47).
- DQ, `Entry<DQ_QuantizerChannel, DQ_CHANNEL_SETTING_LAST, 2, &dqObj>` (:52): the counts are correct (2 × 31).

The generic `set` is only `apply_value` (:25). It is incomplete for both apps:
- **Visible list.** It never calls `update_enabled_settings()` or `cursor.AdjustEnd`, so the firmware's own menu rows go stale after a raw SCALE, SOURCE or TRIGGER write. DQ also needs a rebuild after SCALE1-4, SCALE_SEQ, SEQ_MODE or AUX_OUTPUT.
- **Masks.** It skips `force_update()`, and for masks it skips `last_mask_`. Use `update_scale_mask` instead.
- **DQ slots.** The DQ entry flattens the 4 scale slots into 16 independent settings, with identical names "scale" ×4 and "--> edit" ×4. Which slot is "current" is setting 8 (`SCALE_SEQ`). A raw write of setting 8 misses `reset_scale()`. Friendly mode should show only slot `get_scale_select()`'s SCALE, MASK, ROOT and TRANSPOSE (following the enabled list) and label them per slot.
- **DQ SOURCE names.** DQ SOURCE (idx 14) has max 4, but `value_names` = `cv_input_names` has only 4 entries. `attr.value_names[4]` is out of bounds.
- **No `INT_SEQ_LOOP_START` caveat.** QQ `INT_SEQ_LOOP_START` (idx 37) is rewritten by the ISR at runtime (frame shift), so cached UI values will drift.

## Quadraturia (APP_POLYLFO.ino)

All paths are relative to `src/shoal/firmware/`. Registered as `DECLARE_APP('P','L', "Quadraturia", POLYLFO, POLYLFO_isr)` at OC_apps.ino:60.

### 1. Channels
- There is one settings object, `PolyLfo poly_lfo` (SHOAL_STATE, APP_POLYLFO.ino:236). The UI has no channel selection. Outputs A to D are four phases of one LFO.
- UI state lives in `PolyLfoState poly_lfo_state` (238-245). It holds `left_edit_mode` (a `POLYLFO_SETTINGS`, either COARSE or FINE) and `cursor`.
- SettingsAccess.inc maps this correctly: 1 channel, `poly_lfo`, `POLYLFO_SETTING_LAST` = 21.

### 2. Settings table (enum 36-59, `SETTINGS_DECLARE` 212-234)
`POLYLFO_FREQ_MULT_*` is defined in frames_poly_lfo.h:46-85: `BY16`=0 … `NONE`=19 … `1_OVER_16`=38, `LAST`=39.

| idx | enum | name | min | max | default | value names |
|---|---|---|---|---|---|---|
| 0 | POLYLFO_SETTING_COARSE | "C" | 0 | 255 | 64 | NULL |
| 1 | POLYLFO_SETTING_FINE | "F" | -128 | 127 | 0 | NULL |
| 2 | POLYLFO_SETTING_TAP_TEMPO | "Tap tempo" | 0 | 1 | 0 | `OC::Strings::off_on` = {"off","on"} |
| 3 | POLYLFO_SETTING_SHAPE | "Shape" | 0 | 255 | 0 | NULL (the menu row draws a waveform preview, 398-410) |
| 4 | POLYLFO_SETTING_SHAPE_SPREAD | "Shape spread" | -128 | 127 | 0 | NULL |
| 5 | POLYLFO_SETTING_SPREAD | "Phase/frq sprd" | -128 | 127 | 0 | NULL |
| 6 | POLYLFO_SETTING_COUPLING | "Coupling" | -128 | 127 | 0 | NULL |
| 7 | POLYLFO_SETTING_ATTENUATION | "Output range" | 0 | 230 | 230 | NULL |
| 8 | POLYLFO_SETTING_OFFSET | "Offset" | -128 | 127 | 0 | NULL |
| 9 | POLYLFO_SETTING_FREQ_RANGE | "Freq range" | 0 | 11 | 9 | `freq_range_names` (190-192): "cosm","geol","glacl","snail","sloth","vlazy","lazy","vslow","slow","med","fast","vfast" |
| 10 | POLYLFO_SETTING_FREQ_DIV_B | "B freq ratio" | 0 (BY16) | 38 | 19 (NONE, "unity") | `freq_div_names` (194-198), 39 ratio strings "16/1" … "5/4","unity","4/5" … "1/16" |
| 11 | POLYLFO_SETTING_FREQ_DIV_C | "C freq ratio" | 0 | 38 | 19 | `freq_div_names` |
| 12 | POLYLFO_SETTING_FREQ_DIV_D | "D freq ratio" | 0 | 38 | 19 | `freq_div_names` |
| 13 | POLYLFO_SETTING_B_XOR_A | "B XOR A" | 0 | 8 | 0 | `xor_levels` (200-202): "off","  1".."  8" |
| 14 | POLYLFO_SETTING_C_XOR_A | "C XOR A" | 0 | 8 | 0 | `xor_levels` |
| 15 | POLYLFO_SETTING_D_XOR_A | "D XOR A" | 0 | 8 | 0 | `xor_levels` |
| 16 | POLYLFO_SETTING_B_AM_BY_A | "B AM by A" | 0 | 127 | 0 | NULL |
| 17 | POLYLFO_SETTING_C_AM_BY_B | "C AM by B" | 0 | 127 | 0 | NULL |
| 18 | POLYLFO_SETTING_D_AM_BY_C | "D AM by C" | 0 | 127 | 0 | NULL |
| 19 | POLYLFO_SETTING_CV4 | "CV4: DEST" | 0 | 6 | 0 | `cv4_destinations` (204-206): "cplg","sprd"," rng","offs","a->b","b->c","c->d" |
| 20 | POLYLFO_SETTING_TR4_MULT | "TR4: MULT" | 0 | 5 | 3 | `tr4_multiplier` (208-210): "/8","/4","/2","x2","x4","x8" |

Code comments in the ISR (283-303) suggest CV4 index 1 means shape spread, while its label is "sprd". Index 2 drives attenuation and is labelled " rng". This mismatch is cosmetic.

### 3. Visible settings
- The app has no enabled-settings list and no pages. The cursor is `poly_lfo_state.cursor.Init(POLYLFO_SETTING_TAP_TEMPO, POLYLFO_SETTING_LAST - 1)` (355), so the menu shows rows 2..20 (19 rows) unconditionally.
- **Never shown in the menu:** `POLYLFO_SETTING_COARSE` (0) and `POLYLFO_SETTING_FINE` (1). They are reached only through the L encoder, UP and DOWN. Which of the two the L encoder edits is shown as "(C)" or "(F)" in the title (388/390).
- Nothing is hidden conditionally. However, while `Tap tempo` = on, the L encoder, L push, UP and DOWN are all disabled (434, 437, 440, 468), so COARSE and FINE cannot be edited at all.

### 4. Controls (`POLYLFO_handleButtonEvent` 430-463, `POLYLFO_handleEncoderEvent` 466-476)
- **L encoder** (468): `if (!poly_lfo.get_tap_tempo()) poly_lfo.change_value(poly_lfo_state.left_edit_mode, event.value);` This edits setting 0 or 1 and is encoder-only, with no menu row.
- **R encoder** (469-475): when `cursor.editing()`, it does `change_value(cursor.cursor_pos(), event.value)`, which edits a setting. Otherwise it does `cursor.Scroll`.
- **L push** (439-446): when tap tempo is off, it toggles `poly_lfo_state.left_edit_mode` between COARSE and FINE. This is a UI member variable, not a setting.
- **R push** (447-448): `cursor.toggle_editing()`.
- **UP** (433-434): `change_value(POLYLFO_SETTING_COARSE, 32)` when tap tempo is off. **DOWN** (436-437) does the same with -32. Both are encoder-only edits of setting 0.
- **DOWN long press** (455-456): `poly_lfo.lfo.set_phase_reset_flag(true)`. This is a one-shot phase reset of all four outputs, consumed in `frames::PolyLfo::Render` (frames_poly_lfo.cpp:156-158). The flag is DSP state, not a setting, and the reset does not happen while the LFO is frozen because `Render` is skipped.
- **UP long press** is consumed by OC_ui.cpp:159-167. In Shoal it does nothing (SHOAL comment at 164-165).
- **R long press** opens the global app menu (OC_ui.cpp:168-170).
- **L long press** reaches the app (OC_ui.cpp:173) but is not handled, so it is a no-op.
- `PolyLfo::freeze()/thaw()/frozen_` (150-160) are never called from the UI; a repository-wide grep found no callers. Freeze comes only from the TR2 gate (250, 343).

### 5. Guards and follow-ups
- There are no constraints other than the clamp in `apply_value` and the tap-tempo gating on the L-side controls. Nothing runs after an edit.
- The ISR (`POLYLFO_isr` 247-350) reads every setting through getters on every tick, so a raw `apply_value` takes effect exactly as an encoder edit would.
- Changing `FREQ_DIV_B/C/D` sets `phase_reset_flag_` inside `lfo.set_freq_div_*` when the value changes (frames_poly_lfo.h:179-198). Because this happens in the ISR, a raw write reproduces it too.
- The firmware does nothing when leaving edit mode.

### 6. Masks and sub-editors
None.

### 7. Title bar (`POLYLFO_menu` 378-412, `menu::DefaultTitleBar`)
- With tap tempo on, the title reads `"(T) Ch A: tap tempo"` (382).
- Otherwise it reads `"(%s) Ch A: %6.2f Hz"`, where `%s` is the name of `left_edit_mode`, "C" or "F". At a measured frequency below 0.1 Hz it switches to the period `"%6.3fs"` (388-390), computed from `lfo.get_freq_ch1()`.
- A 4x8 indicator bitmap is drawn at x=122 when `poly_lfo.freq_mult() < 0xFF` (385-386). `freq_mult` is 0xFF when `digitalReadFast(TR4)` is high and is `tr4_multiplier()` otherwise (340). The indicator therefore means the TR4 multiplier is active, but how Shoal maps the TR4 level is not checked here.

### 8. Screensaver
- `POLYLFO_screensaver` calls `OC::scope_render()` (414-416).
- `POLYLFO_handleAppEvent` (418-428): `APP_EVENT_RESUME` calls `cursor.set_editing(false)`. SUSPEND, SCREENSAVER_ON and SCREENSAVER_OFF do nothing.

---

## Low-rents (APP_LORENZ.ino)

Registered at OC_apps.ino:61 as "Low-rents".

### 1. Channels
- There is one settings object, `LorenzGenerator lorenz_generator` (168). Two generators, 1 and 2, share it, with per-generator settings ("Freq 1" and "Freq 2" and so on).
- The UI's "channel" is `lorenz_generator_state.selected_generator` (bool, 171), toggled with L push (317-319). It only selects which of FREQ1 or FREQ2 the L encoder, UP and DOWN edit, and which half of the title bar is highlighted. The settings list is the same in both cases.
- SettingsAccess.inc maps this correctly: 1 channel, `lorenz_generator`, 10 settings.

### 2. Settings table (enum 31-43, `SETTINGS_DECLARE` 150-166)
The `streams::LORENZ_OUTPUT_*` values (streams_lorenz_generator.h:47-69) are X1=0, Y1=1, Z1=2, X2=3, Y2=4, Z2=5, …, LAST=22.

| idx | enum | name | min | max | default | value names |
|---|---|---|---|---|---|---|
| 0 | LORENZ_SETTING_FREQ1 | "Freq 1" | 0 | 255 | 128 (0 under `BUCHLA_4U`, which is not defined: OC_options.h:20,38) | NULL |
| 1 | LORENZ_SETTING_FREQ2 | "Freq 2" | 0 | 255 | 128 (same caveat) | NULL |
| 2 | LORENZ_SETTING_RHO1 | "Rho/c 1" | 4 | 127 | 63 | NULL |
| 3 | LORENZ_SETTING_RHO2 | "Rho/c 2" | 4 | 127 | 63 | NULL |
| 4 | LORENZ_SETTING_FREQ_RANGE1 | "LFreq 1 Rng" | 0 | 4 | 2 | `lorenz_freq_range_names` (146-148): "sloth","lazy","slow","med","fast" |
| 5 | LORENZ_SETTING_FREQ_RANGE2 | "LFreq 2 Rng" | 0 | 4 | 2 | `lorenz_freq_range_names` |
| 6 | LORENZ_SETTING_OUT_A | "Out A " (trailing space) | 0 | 21 | 0 (Lx1) | `lorenz_output_names` (45-68), 22 entries: "Lx1","Ly1","Lz1","Lx2","Ly2","Lz2","Rx1","Ry1","Rz1","Rx2","Ry2","Rz2","Lx1+Rx1","Lx1+Rz1","Lx1+Ly2","Lx1+Lz2","Lx1+Rx2","Lx1+Rz2","Lx1xLy1","Lx1xLx2","Lx1xRx1","Lx1xRx2" |
| 7 | LORENZ_SETTING_OUT_B | "Out B " | 0 | 21 | 1 (Ly1) | `lorenz_output_names` |
| 8 | LORENZ_SETTING_OUT_C | "Out C " | 0 | 21 | 3 (Lx2) | `lorenz_output_names` |
| 9 | LORENZ_SETTING_OUT_D | "Out D " | 0 | 21 | 4 (Ly2) | `lorenz_output_names` |

### 3. Visible settings
- The app has no enabled-settings list and no pages. The cursor is `cursor.Init(LORENZ_SETTING_RHO1, LORENZ_SETTING_LAST - 1)` (239), so rows 2..9 (8 rows) are always shown.
- **Never shown in the menu:** `LORENZ_SETTING_FREQ1` and `LORENZ_SETTING_FREQ2`. They are edited only by the L encoder, UP and DOWN, and their effective value (including CV) appears in the title bar.

### 4. Controls (`LORENZ_handleButtonEvent` 321-338, `LORENZ_handleEncoderEvent` 340-355)
- **L encoder** (342-347): `change_value(selected_generator ? LORENZ_SETTING_FREQ2 : LORENZ_SETTING_FREQ1, event.value)`. This is an encoder-only edit of setting 0 or 1.
- **R encoder** (348-354): when editing, it does `change_value(cursor.cursor_pos(), value)`, which edits a setting. Otherwise it scrolls.
- **L push** (`LORENZ_leftButton` 317-319): `selected_generator = 1 - selected_generator`. This is a UI member, not a setting.
- **R push** (313-315): `cursor.toggle_editing()`.
- **UP** (`LORENZ_topButton` 297-303): +32 on FREQ2 or FREQ1, depending on `selected_generator`. **DOWN** (305-311) applies -32.
- **Long presses:** none are handled by the app (it only handles `EVENT_BUTTON_PRESS`, 322). UP long is consumed by OC_ui (a no-op in Shoal), R long opens the app menu (OC_ui.cpp:168-170), and L long and DOWN long are no-ops.
- `freeze()/thaw()` (115-125) are never called from the UI. Freeze comes from the TR4 gate (183, 228).

### 5. Guards and follow-ups
- There are none beyond the clamp. `LORENZ_isr` (178-235) reads all settings every tick, so a raw `apply_value` is equivalent to an encoder edit.
- The ISR also clamps rho with CV to 4<<8..127<<8 (199-210).

### 6. Masks and sub-editors
None.

### 7. Title bar (`LORENZ_menu` 258-280)
- `menu::DualTitleBar` with column 0 reading `"Freq1 "` plus `USAT16(SCALE8_16(freq1) + cv_freq1*16) >> 8`, which is the live value including CV (261-264).
- Column 1 reads `"Freq2 "` with the same calculation (266-270).
- `DualTitleBar::Selected(selected_generator)` (272) highlights the selected generator.

### 8. Screensaver
- `LORENZ_screensaver` calls `OC::vectorscope_render()` (282-284).
- `LORENZ_handleAppEvent` (286-295) does nothing for any event; even RESUME does not clear editing.

---

## Piqued (APP_ENVGEN.ino)

Registered at OC_apps.ino:62 as "Piqued".

### 1. Channels
- There are four settings objects, `envgen.envelopes_[0..3]` (`EnvelopeGenerator envelopes_[4]`, 834). The container is `QuadEnvelopeGenerator envgen` (SHOAL_STATE, 842).
- The UI calls them A, B, C and D in the `QuadTitleBar` (1061-1063).
- **L encoder** selects the channel (`ui.selected_channel`, 1160-1165), except while the Euclidean sub-editor is active (see 4 and 6).
- SettingsAccess.inc is right about the objects (4 channels, `envgen.envelopes_[ch]`). Its gaps are covered in 5.

### 2. Settings table (enum 44-79, `SETTINGS_DECLARE` 726-760)
Constants used in this table:
- `OC::DIGITAL_INPUT_1..4` = 0..3, and `INT_TRIGGER_LAST` = 1 (122-125).
- `peaks::RESET_BEHAVIOUR_*` = NULL 0, SEGMENT_PHASE 1, SEGMENT_LEVEL_PHASE 2, SEGMENT_LEVEL 3, PHASE 4, with LAST = 5 (peaks_multistage_envelope.h:55-60).
- `FALLING_GATE_BEHAVIOUR_*` = IGNORE 0, HONOUR 1 (64-66).
- `ENV_SHAPE_*` = LINEAR 0 … SINEFOLD 10, with LAST = 11 (40-51).

| idx | enum | name | min | max | default | value names |
|---|---|---|---|---|---|---|
| 0 | ENV_SETTING_TYPE | "TYPE" | 0 | 10 | 0 (AD) | `envelope_types` (695-697): "AD","ADSR","ADR","ASR","ADSAR","ADAR","ADL2","ADRL3","ADL2R","ADAL2R","ADARL4" (enum index 3 is `ENV_TYPE_AR` but displays "ASR") |
| 1 | ENV_SETTING_SEG1_VALUE | "S1" | 0 | 255 | 128 | NULL (shown as "Attack" on the segment page) |
| 2 | ENV_SETTING_SEG2_VALUE | "S2" | 0 | 255 | 128 | NULL ("Decay") |
| 3 | ENV_SETTING_SEG3_VALUE | "S3" | 0 | 255 | 128 | NULL ("Sustain/Level") |
| 4 | ENV_SETTING_SEG4_VALUE | "S4" | 0 | 255 | 128 | NULL ("Release") |
| 5 | ENV_SETTING_TRIGGER_INPUT | "Trigger input" | 0 | 6 (`DIGITAL_INPUT_4 + 3*INT_TRIGGER_LAST`) | 0 in the table, but `Init` applies the channel index, so the defaults are TR1/TR2/TR3/TR4 for A/B/C/D (679, 767-771) | `OC::Strings::trigger_input_names` = {"TR1","TR2","TR3","TR4"} (OC_strings.cpp:21) has **only 4 entries**. Values 4..6 are drawn specially: `'A' + trigger_setting_to_channel_index(v-4)` + " EOC" (991-997, 612-614). Self is skipped: for channel ch, k<ch gives letter k and k>=ch gives letter k+1 |
| 6 | ENV_SETTING_TRIGGER_DELAY_MODE | "Tr delay mode" | 0 | 2 | 0 | `trigger_delay_modes` (707-709): "Off","Queue","Ring" |
| 7 | ENV_SETTING_TRIGGER_DELAY_COUNT | "Tr delay count" | 1 | 24 (`kMaxDelayedTriggers`) | 1 | NULL |
| 8 | ENV_SETTING_TRIGGER_DELAY_MILLISECONDS | "Tr delay msecs" | 0 | 999 | 0 | NULL |
| 9 | ENV_SETTING_TRIGGER_DELAY_SECONDS | "Tr delay secs" | 0 | 64 | 0 | NULL |
| 10 | ENV_SETTING_EUCLIDEAN_LENGTH | "Eucl length" | 0 | 31 | 0 | `euclidean_lengths` (711-716), 32 entries: "Off","  2".." 32", so value v means length v+1 |
| 11 | ENV_SETTING_EUCLIDEAN_FILL | "Fill" | 0 | 32 | 1 | NULL |
| 12 | ENV_SETTING_EUCLIDEAN_OFFSET | "Offset" | 0 | 32 | 0 | NULL |
| 13 | ENV_SETTING_EUCLIDEAN_RESET_INPUT | "Eucl reset" | 0 | 4 | 0 | `OC::Strings::trigger_input_names_none` = {" - ","TR1","TR2","TR3","TR4"} |
| 14 | ENV_SETTING_EUCLIDEAN_RESET_CLOCK_DIV | "Eucl reset div" | 1 | 255 | 1 | NULL |
| 15 | ENV_SETTING_CV1 | "CV1 -> " | 0 | 11 | 0 | `cv_mapping_names` (703-705): "None","Att","Dec","Sus","Rel","ADR","Eleng","Efill","Eoffs","Delay","Ampl","Loops" |
| 16 | ENV_SETTING_CV2 | "CV2 -> " | 0 | 11 | 0 | `cv_mapping_names` |
| 17 | ENV_SETTING_CV3 | "CV3 -> " | 0 | 11 | 0 | `cv_mapping_names` |
| 18 | ENV_SETTING_CV4 | "CV4 -> " | 0 | 11 | 0 | `cv_mapping_names` |
| 19 | ENV_SETTING_ATTACK_RESET_BEHAVIOUR | "Attack reset" | 0 | 4 | 0 (NULL) | `OC::Strings::reset_behaviours`: "None","SP","SLP","SL","P" |
| 20 | ENV_SETTING_ATTACK_FALLING_GATE_BEHAVIOUR | "Att fall gt" | 0 | 1 | 0 | `OC::Strings::falling_gate_behaviours`: "Ignor","Honor" |
| 21 | ENV_SETTING_DECAY_RELEASE_RESET_BEHAVIOUR | "DecRel reset" | 0 | 4 | 1 (SEGMENT_PHASE, "SP") | `OC::Strings::reset_behaviours` |
| 22 | ENV_SETTING_GATE_HIGH | "Gate high" | 0 | 1 | 0 | `OC::Strings::no_yes`: "No","Yes" |
| 23 | ENV_SETTING_ATTACK_SHAPE | "Attack shape" | 0 | 10 | 2 (QUARTIC) | `OC::Strings::envelope_shapes`: "Lin","Exp","Quart","Sine","Ledge","Cliff","Gate","BgDip","MeDip","LtDip","Wiggl" |
| 24 | ENV_SETTING_DECAY_SHAPE | "Decay shape" | 0 | 10 | 1 (EXPONENTIAL) | `envelope_shapes` |
| 25 | ENV_SETTING_RELEASE_SHAPE | "Release shape" | 0 | 10 | 1 | `envelope_shapes` |
| 26 | ENV_SETTING_ATTACK_TIME_MULTIPLIER | "Attack mult" | 0 | 13 | 0 | `time_multipliers` (718-720): "1","  2","  4","  8","  16","  32","  64"," 128"," 256"," 512","1024","2048","4096","8192" |
| 27 | ENV_SETTING_DECAY_TIME_MULTIPLIER | "Decay mult" | 0 | 13 | 0 | `time_multipliers` |
| 28 | ENV_SETTING_RELEASE_TIME_MULTIPLIER | "Release mult" | 0 | 13 | 0 | `time_multipliers` |
| 29 | ENV_SETTING_AMPLITUDE | "Amplitude" | 0 | 127 | 127 | NULL |
| 30 | ENV_SETTING_SAMPLED_AMPLITUDE | "Sampled Ampl" | 0 | 1 | 0 | `no_yes` |
| 31 | ENV_SETTING_MAX_LOOPS | "Max loops" | 0 | 127 | 0 | NULL |
| 32 | ENV_SETTING_INVERTED | "Inverted" | 0 | 1 | 0 | `no_yes` |

`ENV_SETTING_LAST` = 33.

### 3. Visible settings, pages and the segment editor
**Pages.** `ui.edit_mode` is one of `MODE_EDIT_SEGMENTS` (the envelope page, which is the initial page, 773) or `MODE_EDIT_SETTINGS` (809-812).
- `ENVGEN_menu` (1078-1081) draws `ENVGEN_menu_preview()` (903-959) on the segment page and `ENVGEN_menu_settings()` (961-1056) otherwise.
- **L push** switches pages (`ENVGEN_leftButton` 1104-1116). Going from settings to segments happens only `if (!envgen.euclidean_edit_active())`, and also calls `cursor.set_editing(false)`. Going from segments to settings sets `segment_editing = false`.

**Envelope (segment) page.**
- The page shows a single row: name `segment_names[selected_segment]`, which is "Attack", "Decay", "Sustain/Level" or "Release" (699-701), and value `get_segment_value(selected_segment)`, which is setting 1+seg (906-913). Below it is the envelope preview with a segment cursor and loop markers.
- The selectable segments are `0..num_editable_segments()-1` (298-316). The count is **2** for `ENV_TYPE_AD`, `ENV_TYPE_AR` and `ENV_TYPE_ADL2`, and **4** for all other types. So for AD, AR and ADL2, S3 and S4 are unreachable from the segment page.

**Settings page.** It builds an enabled-settings list: `num_enabled_settings()` (352), `enabled_setting_at(i)` (356) and `update_enabled_settings()` (360-402). The order is:
- `ENV_SETTING_TYPE`, `ENV_SETTING_TRIGGER_INPUT`, `ENV_SETTING_TRIGGER_DELAY_MODE`.
- Then `if (get_trigger_delay_mode())` (366): `ENV_SETTING_TRIGGER_DELAY_COUNT`, `ENV_SETTING_TRIGGER_DELAY_MILLISECONDS` and `ENV_SETTING_TRIGGER_DELAY_SECONDS`. These are hidden when the mode is Off.
- Then `ENV_SETTING_EUCLIDEAN_LENGTH`.
- Then `if (get_euclidean_length())` (373): `ENV_SETTING_EUCLIDEAN_OFFSET`, `ENV_SETTING_EUCLIDEAN_RESET_INPUT` and `ENV_SETTING_EUCLIDEAN_RESET_CLOCK_DIV`. `ENV_SETTING_EUCLIDEAN_FILL` is commented out at 374.
- Then the following, always shown: ATTACK/DECAY/RELEASE_SHAPE, ATTACK/DECAY/RELEASE_TIME_MULTIPLIER, CV1..CV4, ATTACK_RESET_BEHAVIOUR, ATTACK_FALLING_GATE_BEHAVIOUR, DECAY_RELEASE_RESET_BEHAVIOUR, GATE_HIGH, AMPLITUDE, SAMPLED_AMPLITUDE, MAX_LOOPS, INVERTED. This order differs from the enum order.
- The list length ranges from 22 (both conditions off) to 28 (both on).
- `indentSetting` (404-418) indents the delay-count, msec and sec rows and the Euclidean fill, offset, reset input and reset div rows.
- **Nothing depends on envelope TYPE.** Type affects only the segment page (number of segments) and the DSP.

**Special row rendering:**
- `TYPE` (976-984): the name is not printed, only the value, with an edit icon.
- `TRIGGER_INPUT` (985-999): see the internal-trigger labels in the table above.
- `EUCLIDEAN_OFFSET` (1000-1010): when not editing, the row draws the live Euclidean mask using CV-modulated `s_euclidean_*` values. When editing, it opens the boxed Euclidean editor (1021-1055).

**Never shown as a settings-page row:**
- `SEG1..SEG4_VALUE`, which are only on the segment page.
- `EUCLIDEAN_FILL`, which is edited only inside the Euclidean editor with the L encoder.
- SEG3 and SEG4 for 2-segment types are not reachable from the segment page either.

### 4. Controls (`ENVGEN_handleButtonEvent` 1118-1135, `ENVGEN_handleEncoderEvent` 1137-1201)
**L encoder** (1139-1166):
- If `euclidean_edit_active()` (803-807: `ui.cursor.editing() && ENV_SETTING_EUCLIDEAN_OFFSET == selected().enabled_setting_at(ui.cursor.cursor_pos())`), it edits **Eucl length** or **Fill** through the sub-editor, depending on `ui.euclidean_edit_length` (see 6).
- Otherwise it selects the channel: `selected_channel = CONSTRAIN(selected_channel + value, 0, 3)`. It then does `CONSTRAIN(ui.selected_segment, 0, selected_env.num_editable_segments() - 1)` and `ui.cursor.AdjustEnd(selected_env.num_enabled_settings() - 1)`. These are UI members only.

**R encoder** (1167-1200):
- On the segment page with `segment_editing`, it does `change_value(ENV_SETTING_SEG1_VALUE + selected_segment, value)` (1171), which edits a setting. Without `segment_editing`, it selects the segment, clamped to `num_editable_segments()-1` (1173-1175). That is a UI member.
- On the settings page while editing, it edits `enabled_setting_at(cursor_pos())`, with guards (see 5). While not editing, it does `cursor.Scroll`.

**L push** (`ENVGEN_leftButton` 1104-1116) does one of two things:
- It switches page (`ui.edit_mode`), clearing `cursor.editing` or `segment_editing`.
- While the Euclidean editor is active, it instead toggles `ui.euclidean_edit_length` (1110). That selects whether the L encoder edits Length or Fill. All of this is UI state.

**R push** (`ENVGEN_rightButton` 1094-1102):
- On the segment page it toggles `ui.segment_editing`.
- On the settings page it does `cursor.toggle_editing()` and `ui.euclidean_edit_length = false`.

**UP / DOWN** (`ENVGEN_topButton/lowerButton` 1084-1092):
- They apply `change_value(ENV_SETTING_SEG1_VALUE + ui.selected_segment, ±32)` to the selected channel **on either page**. The settings page gives no visual feedback.
- `selected_segment` is clamped only on channel change or R scroll, not on a TYPE change. After switching TYPE to AD or AR or ADL2 while segment 3 or 4 was selected, UP and DOWN can still edit S3 or S4.

**Long presses:**
- The app handles none (only `EVENT_BUTTON_PRESS`, 1119).
- UP long press is a no-op in Shoal (OC_ui.cpp:159-167).
- R long press opens the app menu (OC_ui.cpp:168-170).
- L long and DOWN long are no-ops.

**Encoder-only functions:**
- Segment values S1 to S4 are edited on their own page, not as menu rows.
- Fill and Length are edited through the Euclidean editor.
- Channel select, page switch and segment select are UI state, not settings.

### 5. Guards and follow-ups on an R-encoder settings edit (1178-1195)
**Guard on `EUCLIDEAN_OFFSET`** (1182-1188):
- `if (selected_env.get_euclidean_offset() < selected_env.get_euclidean_length()) change_value(...) else if (event.value < 0) change_value(...)`.
- In effect, the offset is limited to `<= euclidean_length` (the setting value, which is the real length minus 1), not the declared max of 32.

**Other settings:**
- They get a plain `change_value(setting, event.value)` (1190).
- **There are no trigger-input restrictions in this version.** `ENV_SETTING_TRIGGER_INPUT` is not special-cased in the edit path. It is a plain 0..6 clamp, and self-trigger is impossible only because of the encoding (`trigger_setting_to_channel_index` skips its own channel, 612-614).

**Follow-up** (1193-1195):
```
if (ENV_SETTING_TRIGGER_DELAY_MODE == setting || ENV_SETTING_EUCLIDEAN_LENGTH == setting)
  selected_env.update_enabled_settings();
  envgen.ui.cursor.AdjustEnd(selected_env.num_enabled_settings() - 1);
```
The braces are missing, so `AdjustEnd` runs after **every** edit; only `update_enabled_settings` is conditional. The behaviour is harmless.

**`AdjustEnd` does not clamp the cursor position** (OC_menus.h:83-86: "WARN … we don't have to adjust screen line"). Two cases arise:
- Editing DELAY_MODE or LENGTH happens on the cursor row itself, and that row sits above the inserted or removed rows, so this is fine.
- When the **L encoder switches to a channel with a shorter list**, `cursor_pos` can sit beyond the new end. `enabled_setting_at` then returns stale entries from `enabled_settings_[ENV_SETTING_LAST]`. These are in bounds but stale, and this is an upstream quirk.

**Nothing happens on leaving edit mode** apart from R push resetting `euclidean_edit_length`.

**Restore** (860-869): calls `update_enabled_settings()` for all 4 channels, then `AdjustEnd` using **channel 0's** count, not the selected channel's.

**Raw `apply_value` (SettingsAccess `set`):**
- The ISR reads every setting every tick. `QuadEnvelopeGenerator::ISR` (782-801) calls `EnvelopeGenerator::Update` (420-580), so the DSP effect of a raw write matches an encoder edit.
- That includes a TYPE change, which triggers `env_.reset()` via `last_type_` (476-479).
- **However, a raw write to `ENV_SETTING_TRIGGER_DELAY_MODE` or `ENV_SETTING_EUCLIDEAN_LENGTH` leaves `enabled_settings_` stale.** To match the encoder path, the caller must call `envelopes_[ch].update_enabled_settings()`. If ch is the selected channel, it must also call `envgen.ui.cursor.AdjustEnd(num_enabled_settings()-1)`, and should clamp `cursor_pos` itself.
- A raw write also bypasses the offset guard above and the length/fill/offset cross-clamping in 6.

### 6. Sub-editors
**Euclidean editor** (boxed overlay, 1021-1055).
- **Opens when:** on the settings page, with the cursor on the `Offset` row (`ENV_SETTING_EUCLIDEAN_OFFSET`, visible only when length > 0), and R push enters edit mode. While open:
  - **R encoder** edits Offset, with the guard in 5.
  - **L encoder** edits **Fill** by default (`euclidean_edit_length == false`). The guard is `if (fill <= length) change_value(FILL, v) else if (v < 0) change_value(FILL, v)`, so fill can reach `length+1`, which equals the real step count (1153-1157).
  - **L push** toggles to editing **Length** (shown as "Len", min forced to 1 for display, 1040-1044). The code is `int length = get_euclidean_length() + v; if (length > 0) { apply_value(LENGTH, length); if (length < fill) apply_value(FILL, length + 1); if (length < offset) apply_value(OFFSET, length); }` (1141-1151).
  - In the sub-editor, length can never be set to 0 ("Off"). This path does **not** call `update_enabled_settings` (not needed, since length stays above 0) and does not call `AdjustEnd`.
- **Writes back** directly through `apply_value` and `change_value`.
- **Closes** on R push, which calls `toggle_editing()` and resets `euclidean_edit_length`. L push does not close it.
- **Meaningful values:**
  - fill is 0..length+1.
  - offset is 0..length.
  - length is 1..31 inside the editor, or 0..31 from the "Eucl length" menu row.
- The menu-row path does not clamp fill or offset after the length shrinks. The ISR clamps the modulated copies anyway (445-447).

**Segment editor** (the envelope page):
- It is not a modal sub-editor. It is the page itself: R push toggles `segment_editing`, and R turn changes S1+seg.
- The meaningful segment count is `num_editable_segments()`, 2 or 4.
- For ADSR-family types, S3 (sustain) is used as `>>1` (459-468).

**Masks:** none. Euclidean is a pattern displayed through `OC::EuclideanMaskDraw`, not a mask setting.

### 7. Title bar (`ENVGEN_menu` 1058-1082, `menu::QuadTitleBar`)
- It shows the letters A to D (1063).
- Each channel has a gate/trigger indicator, `DrawGateIndicator(i, getTriggerState())` (1064).
- Each channel has a delayed-trigger countdown bar. The bar is 2 px wide at `ColumnStartX(i)+28`, with height `time_left*8/delay`, drawn when the next delayed trigger has `delay != 0` (1067-1073).
- `Selected(selected_channel)` highlights the selected channel.
- The envelope type is not shown in the title bar.

### 8. Screensaver
- `ENVGEN_screensaver` (1218-1241) draws four fast envelope previews in quadrants, A top-left, B top-right, C bottom-left, D bottom-right (`BUCHLA_4U` is off), plus `OC::scope_render()`.
- `ENVGEN_handleAppEvent` (871-880) does nothing for any event; even RESUME does not clear editing.

---


## Sequins (APP_A_SEQ.ino)

File confirmed: `src/shoal/firmware/APP_A_SEQ.ino`, 2636 lines (there is no `APP_SEQ.ino`). Registered in `OC_apps.ino:63` as `DECLARE_APP('S','Q', "Sequins", SEQ, SEQ_isr)` (line 47 is an `#ifdef`'d alternative named "2x Sequencer"). All paths below are relative to `src/shoal/firmware/` unless noted. Line numbers are from APP_A_SEQ.ino unless another file is named.

### 1. Channels

- `const uint8_t NUM_CHANNELS = 2;` (44). Settings objects: `SEQ_Channel seq_channel[NUM_CHANNELS]` (2066-2067), class `SEQ_Channel : settings::SettingsBase<SEQ_Channel, SEQ_CHANNEL_SETTING_LAST>` (274). `SEQ_CHANNEL_SETTING_LAST` = 56.
- The UI calls them `#A` and `#B` (title bar, `graphics.print("#"); graphics.print((char)('A' + i));` 2424-2425). Channel A drives DAC A (pitch) + C (aux); channel B drives DAC B + D (2150-2157).
- **L encoder** selects the channel (2217-2227): `selected_channel = seq_state.selected_channel + event.value`, clamped 0..1. `seq_state.selected_channel` is a member of `SEQ_State` (2057), not a setting and not saved.
- **Each channel holds 4 sequences.** Each channel has its own MASK1..4 and SEQUENCE_LEN1..4 settings (one per sequence, so 4 masks and 4 lengths per channel). The step pitches are not settings. They live in the global `OC::g_user_patterns[8]` (`OC_patterns.h:41`, `PATTERN_USER_ALL` = 8): channel A uses slots 0..3 and channel B uses slots 4..7 (`_channel_offset = !channel_id_ ? 0 : OC::Patterns::NUM_PATTERNS`, 625, 633, 645). Each slot is `int16_t notes[16]` (`OC_patterns_presets.h:10`). They are saved in global settings (`OC_apps.ino:196`, restored at `:369`), not in the app's `SEQ_save` (2085-2092).

### 2. Settings table

Enum `SEQ_ChannelSetting` at 131-195; `SETTINGS_DECLARE(SEQ_Channel, SEQ_CHANNEL_SETTING_LAST)` at 1976-2038 (`{default, min, max, name, value_names, storage}`). The enum and the declare table line up one to one (56 entries each).

Value-name arrays:
- `modes` (1960): `"gate","copy","AD","ADR","ADSR"`
- `SEQ_CHANNEL_TRIGGER_sources` (1948): `"TR1","TR3"," - "`
- `reset_trigger_sources` (1952): `"RST2","RST4"," - ","=HI2","=LO2","=HI4","=LO4"`
- `display_multipliers` (1956), 27 entries: `"/64","/63","/62","/49","/48","/47","/33","/32","/31","/17","/16","/15","/8","/7","/6","/5","/4","/3","/2","-","x2","x3","x4","x5","x6","x7","x8"` (index 19 `"-"` = x1)
- `cv_ranges` (1964): `" 5V","10V"`
- `arp_directions` (1968): `"up","down","u/d","rnd"`
- `arp_range` (1972): `"1","2","3","4"`
- `OC::Strings::trigger_delay_times` (`OC_strings.cpp:39`), 8 entries: `"off","120us","240us","360us","480us","1ms","2ms","4ms"`
- `OC::scale_names_short` (`OC_scales.cpp:28`), 133 entries: `USER1..USER4`, `"OFF "`, `SEMI`, `IONI`, `DORI`, ... The menu row draws the long names `OC::scale_names[value]` instead (2457). `NUM_SCALES` = 4 user + 129 braids = 133 (`OC_scales.cpp:12`). `SCALE_NONE` = 4 ("OFF"), `SCALE_SEMI` = 5.
- `OC::Strings::note_names_unpadded` (`OC_strings.cpp:17`): `C C# D D# E F F# G G# A A# B`
- `OC::pattern_names_short` (`OC_patterns.cpp:19`): `"SEQ-1","SEQ-2","SEQ-3","SEQ-4"` (a 5th entry, "DEFAULT", is out of range)
- `OC::Strings::seq_playmodes` (`OC_strings.cpp:7`), 16 entries: `" -","SEQ+1","SEQ+2","SEQ+3","TR+1","TR+2","TR+3","ARP","S+H#1","S+H#2","S+H#3","S+H#4","CV#1","CV#2","CV#3","CV#4"` (enum `PLAY_MODES` 232-250: PM_NONE=0, PM_SEQ1..3=1..3, PM_TR1..3=4..6, PM_ARP=7, PM_SH1..4=8..11, PM_CV1..4=12..15)
- `OC::Strings::seq_directions` (`OC_strings.cpp:11`): `"fwd","rev","pnd1","pnd2","rnd","brwn"` (enum `SEQ_DIRECTIONS` 252-260; BROWNIAN=5)
- `OC::Strings::cv_input_names_none` (`OC_strings.cpp:27`): `" - ","CV1","CV2","CV3","CV4"`
- `OC::Strings::envelope_shapes` (`OC_strings.cpp:47`), 11 entries: `"Lin","Exp","Quart","Sine","Ledge","Cliff","Gate","BgDip","MeDip","LtDip","Wiggl"` (peaks enum: LINEAR=0, EXPONENTIAL=1, QUARTIC=2, ...)
- `OC::Strings::reset_behaviours` (`OC_strings.cpp:72`): `"None","SP","SLP","SL","P"` (NULL=0, SEGMENT_PHASE=1)
- `OC::Strings::falling_gate_behaviours` (`OC_strings.cpp:76`): `"Ignor","Honor"`

| idx | enum (`SEQ_CHANNEL_SETTING_` +) | name string | min | max | default | value names | storage | decl line |
|---|---|---|---|---|---|---|---|---|
| **Core / clock** |
| 0 | `MODE` | `"aux. mode"` | 0 | 4 | 0 (gate) | `modes` | U4 | 1978 |
| 1 | `CLOCK` | `"clock src"` | 0 | 2 (`SEQ_CHANNEL_TRIGGER_NONE`) | 0 (TR1) | `SEQ_CHANNEL_TRIGGER_sources` | U4 | 1979 |
| 2 | `TRIGGER_DELAY` | `"trigger delay"` | 0 | 7 (`kNumDelayTimes-1`) | 0 (off) | `trigger_delay_times` | U8 | 1980 |
| 3 | `RESET` | `"reset/mute"` | 0 | 6 | 2 (" - ") | `reset_trigger_sources` | U8 | 1981 |
| 4 | `MULT` | `"mult/div"` | 0 | 26 (`MULT_MAX`) | 19 (`MULT_BY_ONE`, "-") | `display_multipliers` | U8 | 1982 |
| 5 | `PULSEWIDTH` | `"--> pw"` | 0 | 255 (`PULSEW_MAX`) | 25 | NULL (drawn specially: 0 = "echo", 255 = "50%", else the number; ms) | U8 | 1983 |
| **Scale / pitch** |
| 6 | `SCALE` | `"scale"` | 0 | 132 (`NUM_SCALES-1`) | 5 (`SCALE_SEMI`) | `OC::scale_names_short` (row draws `OC::scale_names`) | U8 | 1985 |
| 7 | `OCTAVE` | `"octave"` | -5 | 5 | 0 | NULL | I8 | 1986 |
| 8 | `ROOT` | `"root"` | 0 | 11 | 0 (C) | `note_names_unpadded` | U8 | 1987 |
| 9 | `OCTAVE_AUX` | `"--> aux +/-"` | -5 | 5 | 0 | NULL | I8 | 1988 |
| 10 | `SCALE_MASK` | `"--> edit"` | **1** | 65535 | 65535 | NULL (bit mask) | U16 | 1989 |
| **Per-sequence step masks (one per sequence)** |
| 11 | `MASK1` | `"--> edit"` | 0 | 65535 | 65535 | NULL (step mask, seq 1) | U16 | 1991 |
| 12 | `MASK2` | `"--> edit"` | 0 | 65535 | 65535 | NULL (seq 2) | U16 | 1992 |
| 13 | `MASK3` | `"--> edit"` | 0 | 65535 | 65535 | NULL (seq 3) | U16 | 1993 |
| 14 | `MASK4` | `"--> edit"` | 0 | 65535 | 65535 | NULL (seq 4) | U16 | 1994 |
| 15 | `SEQUENCE` | `"sequence #"` | 0 | 3 (`PATTERN_USER_LAST-1`) | 0 (`PATTERN_USER_0_1`) | `OC::pattern_names_short` | U8 | 1995 |
| **Per-sequence lengths (one per sequence)** |
| 16 | `SEQUENCE_LEN1` | `"sequence length"` | 2 (`kMin`) | 16 (`kMax`) | 16 | NULL (seq 1) | U8 | 1996 |
| 17 | `SEQUENCE_LEN2` | `"sequence length"` | 2 | 16 | 16 | NULL (seq 2) | U8 | 1997 |
| 18 | `SEQUENCE_LEN3` | `"sequence length"` | 2 | 16 | 16 | NULL (seq 3) | U8 | 1998 |
| 19 | `SEQUENCE_LEN4` | `"sequence length"` | 2 | 16 | 16 | NULL (seq 4) | U8 | 1999 |
| **Play mode / direction** |
| 20 | `SEQUENCE_PLAYMODE` | `"playmode"` | 0 | 15 (`PM_LAST-1`) | 0 (" -") | `seq_playmodes` | U8 | 2000 |
| 21 | `SEQUENCE_DIRECTION` | `"direction"` | 0 | 5 | 0 (fwd) | `seq_directions` | U8 | 2001 |
| 22 | `SEQUENCE_PLAYMODE_CV_RANGES` | `"CV adr. range"` | 0 | 1 | 0 (" 5V") | `cv_ranges` | U4 | 2002 |
| 23 | `SEQUENCE_ARP_DIRECTION` | `"direction"` (same text as 21) | 0 | 3 | 0 (up) | `arp_directions` | U4 | 2003 |
| 24 | `SEQUENCE_ARP_RANGE` | `"arp.range"` | 0 | 3 | 0 ("1") | `arp_range` | U8 | 2004 |
| 25 | `BROWNIAN_PROBABILITY` | `"-->brown prob"` | 0 | 255 | 64 | NULL | U8 | 2005 |
| **CV sources (all 0..4, default 0, `cv_input_names_none`)** |
| 26 | `MULT_CV_SOURCE` | `"mult/div CV ->"` | 0 | 4 | 0 | `cv_input_names_none` | U4 | 2007 |
| 27 | `TRANSPOSE_CV_SOURCE` | `"transpose   ->"` | 0 | 4 | 0 | `cv_input_names_none` | U4 | 2008 |
| 28 | `PULSEWIDTH_CV_SOURCE` | `"--> pw      ->"` | 0 | 4 | 0 | `cv_input_names_none` | U4 | 2009 |
| 29 | `OCTAVE_CV_SOURCE` | `"octave  +/- ->"` | 0 | 4 | 0 | `cv_input_names_none` | U4 | 2010 |
| 30 | `ROOT_CV_SOURCE` | `"root    +/- ->"` | 0 | 4 | 0 | `cv_input_names_none` | U4 | 2011 |
| 31 | `OCTAVE_AUX_CV_SOURCE` | `"--> aux +/- ->"` | 0 | 4 | 0 | `cv_input_names_none` | U4 | 2012 |
| 32 | `SEQ_CV_SOURCE` | `"sequence #  ->"` | 0 | 4 | 0 | `cv_input_names_none` | U4 | 2013 |
| 33 | `SCALE_MASK_CV_SOURCE` | `"mask rotate ->"` | 0 | 4 | 0 | `cv_input_names_none` | U4 | 2014 |
| 34 | `SEQUENCE_ARP_DIRECTION_CV_SOURCE` | `"direction   ->"` | 0 | 4 | 0 | `cv_input_names_none` | U4 | 2015 |
| 35 | `SEQUENCE_ARP_RANGE_CV_SOURCE` | `"arp.range   ->"` | 0 | 4 | 0 | `cv_input_names_none` | U4 | 2016 |
| 36 | `DIRECTION_CV_SOURCE` | `"direction   ->"` (same text as 34) | 0 | 4 | 0 | `cv_input_names_none` | U4 | 2017 |
| 37 | `BROWNIAN_CV_SOURCE` | `"-->brwn.prb ->"` | 0 | 4 | 0 | `cv_input_names_none` | U4 | 2018 |
| 38 | `LENGTH_CV_SOURCE` | `"seq.length  ->"` | 0 | 4 | 0 | `cv_input_names_none` | U4 | 2019 |
| 39 | `ENV_ATTACK_CV_SOURCE` | `"att dur  ->"` | 0 | 4 | 0 | `cv_input_names_none` | U4 | 2020 |
| 40 | `ENV_DECAY_CV_SOURCE` | `"dec dur  ->"` | 0 | 4 | 0 | `cv_input_names_none` | U4 | 2021 |
| 41 | `ENV_SUSTAIN_CV_SOURCE` | `"sus lvl  ->"` | 0 | 4 | 0 | `cv_input_names_none` | U4 | 2022 |
| 42 | `ENV_RELEASE_CV_SOURCE` | `"rel dur  ->"` | 0 | 4 | 0 | `cv_input_names_none` | U4 | 2023 |
| 43 | `ENV_LOOPS_CV_SOURCE` | `"env loops ->"` | 0 | 4 | 0 | `cv_input_names_none` | U8 | 2024 |
| **Hidden flag** |
| 44 | `DUMMY` | `"-"` | 0 | 1 | 0 | NULL. Stores the "wait for end of sequence" flag (`wait_for_EoS_`); comment `// DUMMY, use to store update behaviour` | U4 | 2025 |
| **Aux envelope** |
| 45 | `ENV_ATTACK_DURATION` | `"--> att dur"` | 0 | 255 | 128 | NULL | U8 | 2027 |
| 46 | `ENV_ATTACK_SHAPE` | `"--> att shape"` | 0 | 10 | 2 (Quart) | `envelope_shapes` | U16 | 2028 |
| 47 | `ENV_DECAY_DURATION` | `"--> dec dur"` | 0 | 255 | 128 | NULL | U8 | 2029 |
| 48 | `ENV_DECAY_SHAPE` | `"--> dec shape"` | 0 | 10 | 1 (Exp) | `envelope_shapes` | U16 | 2030 |
| 49 | `ENV_SUSTAIN_LEVEL` | `"--> sus lvl"` | 0 | 255 | 128 | NULL | U16 | 2031 |
| 50 | `ENV_RELEASE_DURATION` | `"--> rel dur"` | 0 | 255 | 128 | NULL | U8 | 2032 |
| 51 | `ENV_RELEASE_SHAPE` | `"--> rel shape"` | 0 | 10 | 1 (Exp) | `envelope_shapes` | U16 | 2033 |
| 52 | `ENV_MAX_LOOPS` | `"--> loops"` | 1 | 127 | 1 | NULL | U8 | 2034 |
| 53 | `ENV_ATTACK_RESET_BEHAVIOUR` | `"att reset"` | 0 | 4 | 0 (None) | `reset_behaviours` | U8 | 2035 |
| 54 | `ENV_ATTACK_FALLING_GATE_BEHAVIOUR` | `"att fall gt"` | 0 | 1 | 0 (Ignor) | `falling_gate_behaviours` | U8 | 2036 |
| 55 | `ENV_DECAY_RELEASE_RESET_BEHAVIOUR` | `"dec/rel reset"` | 0 | 4 | 1 (SP) | `reset_behaviours` | U8 | 2037 |

Name collisions that a friendly list must break with its own labels: 10-14 are all `"--> edit"`; 16-19 are all `"sequence length"`; 21 and 23 are both `"direction"`; 34 and 36 are both `"direction   ->"`. Row 5's label is replaced at draw time by `"--> sus dur"` when `get_aux_mode() >= ENV_AD` (2476-2481).

### 3. Visible settings and menu pages

- The enabled list is built by `update_enabled_settings(uint8_t channel_id)` (1649-1839); `channel_id` is unused. The list is stored in `enabled_settings_[SEQ_CHANNEL_SETTING_LAST]` (1940). Count: `num_enabled_settings()` (683). Row lookup: `enabled_setting_at(index)` (1645).
- **Two pages per channel**, held in the member `menu_page_` (1897, enum `MENU_PAGES { PARAMETERS, CV_MAPPING }` 221-224). Each channel has its own page; the page is not a setting and is not saved (`Init` sets `PARAMETERS`, 777). **DOWN** (short) switches between the pages (2289-2311). **UP** (short) on CV_MAPPING returns to PARAMETERS (2282-2286). **R push on a `DUMMY` row** returns to PARAMETERS (2353-2356). SUSPEND / SCREENSAVER_ON / SCREENSAVER_OFF force PARAMETERS (2117-2124).

**PARAMETERS page (1655-1742), in order:**
1. `SCALE`, `SCALE_MASK`, `SEQUENCE` (always)
2. One of `MASK1..MASK4`, chosen by `switch (get_sequence())` (1661-1677). Only the selected sequence's mask is listed.
3. `SEQUENCE_PLAYMODE` (always)
4. If `get_playmode() < PM_SH1` (1681):
   - `(get_playmode() == PM_ARP) ? SEQUENCE_ARP_DIRECTION : SEQUENCE_DIRECTION` (1683)
   - then `if (get_playmode() == PM_ARP)` → `SEQUENCE_ARP_RANGE`; `else if (get_direction() == BROWNIAN)` → `BROWNIAN_PROBABILITY` (1684-1687)
   - then `MULT` (1688)

   Otherwise (S+H and CV play modes): `SEQUENCE_PLAYMODE_CV_RANGES` (1690-1691)
5. `OCTAVE`, `ROOT`, `MODE` (always, 1693-1696)
6. Depends on `get_aux_mode()` (1698-1736):
   - `GATE`: `PULSEWIDTH`
   - `COPY`: `OCTAVE_AUX`
   - `ENV_AD`: `ENV_ATTACK_DURATION, ENV_ATTACK_SHAPE, ENV_DECAY_DURATION, ENV_DECAY_SHAPE, PULSEWIDTH, ENV_MAX_LOOPS`
   - `ENV_ADR`: `ENV_ATTACK_DURATION, ENV_ATTACK_SHAPE, ENV_DECAY_DURATION, ENV_DECAY_SHAPE, PULSEWIDTH, ENV_SUSTAIN_LEVEL, ENV_RELEASE_DURATION, ENV_RELEASE_SHAPE, ENV_MAX_LOOPS`
   - `ENV_ADSR`: same as ADR but **without** `ENV_MAX_LOOPS`. `get_max_loops()` is still applied in ADSR (1117, 1148), so the value is only hidden.
7. If `get_playmode() < PM_SH1`: `RESET`, `CLOCK` (1738-1741)

**CV_MAPPING page (1745-1834), in order.** It is laid out to line up row by row with PARAMETERS:
1. `TRANSPOSE_CV_SOURCE`, `SCALE_MASK_CV_SOURCE`, `SEQ_CV_SOURCE`
2. The current `MASK1..4` again, by `get_sequence()` (1751-1767). R push here opens the pattern editor too.
3. `LENGTH_CV_SOURCE` (lines up with "playmode")
4. If `get_playmode() < PM_SH1`:
   - `if (get_playmode() == PM_ARP)` → `SEQUENCE_ARP_DIRECTION_CV_SOURCE, SEQUENCE_ARP_RANGE_CV_SOURCE`; `else` → `DIRECTION_CV_SOURCE` (1773-1777)
   - `if (get_playmode() != PM_ARP && get_direction() == BROWNIAN)` → `BROWNIAN_CV_SOURCE` (1779-1780)
   - then `MULT_CV_SOURCE`

   Otherwise: `DUMMY` (placeholder for "range", 1786)
5. `OCTAVE_CV_SOURCE`, `ROOT_CV_SOURCE`, `DUMMY` (placeholder for "mode", 1788-1790)
6. Depends on aux mode:
   - `GATE`: `PULSEWIDTH_CV_SOURCE`
   - `COPY`: `OCTAVE_AUX_CV_SOURCE`
   - `ENV_AD`: `ENV_ATTACK_CV_SOURCE, ENV_DECAY_CV_SOURCE, ENV_LOOPS_CV_SOURCE, ENV_ATTACK_RESET_BEHAVIOUR, ENV_ATTACK_FALLING_GATE_BEHAVIOUR, ENV_DECAY_RELEASE_RESET_BEHAVIOUR`
   - `ENV_ADR` / `ENV_ADSR`: `ENV_ATTACK_CV_SOURCE, ENV_DECAY_CV_SOURCE, ENV_SUSTAIN_CV_SOURCE, ENV_RELEASE_CV_SOURCE, ENV_LOOPS_CV_SOURCE, ENV_ATTACK_RESET_BEHAVIOUR, ENV_ATTACK_FALLING_GATE_BEHAVIOUR, ENV_DECAY_RELEASE_RESET_BEHAVIOUR`

   (1792-1821)
7. If `get_playmode() < PM_SH1`: `TRIGGER_DELAY`, `CLOCK` (1823-1826). The comment on `CLOCK` says `// = reset source`, but the row is the real `CLOCK` setting, so it appears on both pages.
8. Four trailing `DUMMY` rows (1828-1831). They are drawn as `"-"` with no value (`DrawNoValue<false>`, 2483-2485).

**Only on CV_MAPPING:** `TRIGGER_DELAY` and the three env reset/falling-gate behaviours (53-55), which are not CV sources.

**Never shown as a menu row:** `SEQUENCE_LEN1..4` (16-19; edited only in the pattern editor); the three MASKn of the sequences that are not selected (reachable only through the pattern editor's UP/DOWN); `DUMMY` (44; appears only as `"-"` placeholder rows, and its value is never drawn or edited from the menu).

**List quirks (firmware behaviour, relevant to "match the encoders"):**
- After an R-encoder edit, `update_enabled_settings` is re-run only for `MODE`, `SEQUENCE_PLAYMODE` and `SEQUENCE_DIRECTION` (2255-2260). Editing `SEQUENCE` does **not** rebuild the list, so the MASKn row stays the old sequence's enum until something else rebuilds it. This is cosmetic on hardware, because the row draws `get_display_mask()` rather than the setting value, and R push opens the editor on `get_sequence()` whichever MASKn the row holds.
- Page switches (2283-2285, 2308-2310, 2354-2355, 2121-2123) rebuild the list but **do not** call `cursor.AdjustEnd`. The cursor range stays at the previous page's row count, so on the new page the cursor can stop short of the last rows or scroll onto stale `enabled_settings_` entries past `num_enabled_settings_`. `SettingsList` always draws `kScreenLines` rows from `first_visible()` (`OC_menus.h:435-455`) and does not check the list count. The trailing DUMMY padding on CV_MAPPING presumably exists to hide this.

### 4. Controls

Dispatch: `SEQ_handleButtonEvent` (2160-2204), `SEQ_handleEncoderEvent` (2206-2269). While the pattern editor or scale editor is open, it gets all encoder events (2208-2215) and all short presses (2179-2186). Long presses partly bypass this, see below. `OC_ui.cpp:158-174`: **UP long** is swallowed in Shoal (does nothing, 164-165), so `SEQ_upButtonLong` (2387-2389, empty anyway) is unreachable. **R long** opens the global app menu (`AppMenuOpen`, 168-170) and never reaches the app. Other long presses go to the app.

| Control | Effect (menu, no editor open) | State changed | Is it a setting? |
|---|---|---|---|
| L encoder | Select channel (2217-2227); `update_enabled_settings`; `cursor.Init(SEQ_CHANNEL_SETTING_MODE, 0)` (cursor to top, editing off); `AdjustEnd` | `seq_state.selected_channel`, cursor | No (`SEQ_State` member) |
| R encoder, not editing | `cursor.Scroll` (2266) | cursor | No |
| R encoder, editing | `change_value(setting, event.value)` on the row's setting (2239), with follow-ups (§5) | the setting | Yes |
| R push | By row (`SEQ_rightButton` 2324-2361): `SCALE` → `toggle_editing()` + `update_scale(true)` (2330-2333). `SCALE_MASK` → opens scale editor if `OC::Scales::SCALE_NONE != scale` (2334-2341). `MASK1..4` → opens pattern editor on `get_sequence()` if `!= PATTERN_NONE` (always true, since max is 3) (2342-2352). `DUMMY` → page = PARAMETERS + `update_enabled_settings` (2353-2356). Any other row → `toggle_editing()` (2357-2359) | cursor editing flag; editor open; `menu_page_` | No |
| L push | `SEQ_leftButton` (2363-2367): `sync()` on **both** channels | `pending_sync_ = true` (701); the next clock resets `div_cnt_` / `clk_cnt_` (976, 1024-1027) | No (member) |
| UP push | `SEQ_upButton` (2271-2287). On PARAMETERS: `octave_toggle()` flips the per-channel `octave_toggle_` and then `change_value(SEQ_CHANNEL_SETTING_OCTAVE, +1)` if the flag is now set, else `-1`. Successive presses alternate +1/−1, so the octave toggles between two values. On CV_MAPPING: page = PARAMETERS, rebuild, `set_editing(false)` | `OCTAVE` setting (7) **and** member `octave_toggle_` (not saved; drawn as "+" in title bar); or `menu_page_` | OCTAVE yes; toggle flag no |
| DOWN push | `SEQ_downButton` (2289-2311): toggle page PARAMETERS ↔ CV_MAPPING, rebuild, `set_editing(false)` | `menu_page_` | No |
| L long | `SEQ_leftButtonLong` (2369-2385), only if no editor is open: **copy scale to the other channel**. `other.set_scale(scale)`, `other.update_scale_mask(get_rotated_scale_mask(), DUMMY)`, `other.update_scale(true)` | other channel's `SCALE` (6) and `SCALE_MASK` (10). The mask copied is the *effective* mask `last_scale_mask_`, which includes any CV rotation | Yes (other channel) |
| DOWN long | `SEQ_downButtonLong` (2391-2403). On CV_MAPPING: `clear_CV_mapping()` (sets settings 26-43 to 0, 704-723) + `set_editing(false)`. On PARAMETERS: `toggle_EoS()` on **both** channels (294-297) | CV_MAPPING: settings 26-43. PARAMETERS: member `wait_for_EoS_` and setting `DUMMY` (44) on both channels | Yes |
| UP long | Nothing (swallowed by `OC_ui.cpp:159-166`) | none | |
| R long | Global app menu (`OC_ui.cpp:168-170`; `OC_apps.ino:447-454`), which sends `APP_EVENT_SUSPEND` | see §8 | No |

**Encoder-only functions (no menu row):** channel select (L enc); scrolling and edit toggle; opening the scale or pattern editor; manual sync (L push); octave toggle (UP); page switch (DOWN); copy scale to the other channel (L long); clear CV mapping / toggle EoS update behaviour (DOWN long); everything inside the editors (§6).

**Long-press quirk:** `SEQ_handleButtonEvent` calls `SEQ_downButtonLong()` for **every** DOWN long press *before* the editor check (2167-2168), so it also runs while an editor is open. In the pattern editor, DOWN long therefore also toggles EoS on both channels (page PARAMETERS) or clears the CV mapping (page CV_MAPPING, since the editor can be opened from the CV page's MASKn row), as well as pasting or clearing. L long is guarded for the pattern editor only (2171); `SEQ_leftButtonLong` itself also returns early while the scale editor is open (2371).

### 5. Guards and follow-ups on an R-encoder edit (2229-2264)

- **Guard:** `if (SEQ_CHANNEL_SETTING_SCALE_MASK != setting || SEQ_CHANNEL_SETTING_MASK1 != setting || ... || SEQ_CHANNEL_SETTING_MASK4 != setting)` (2237). It joins the tests with `||`, so it is **always true** and filters nothing. In practice the mask rows can never be in edit mode, because R push on them opens an editor instead of `toggle_editing()`.
- **Edit:** `if (selected.change_value(setting, event.value)) selected.force_update();` (2239-2240). `change_value` = `apply_value(values_[i] + delta)`, which clamps to [min,max] (`util/util_settings.h:86-99`). `force_update()` only sets `force_update_` (857-859), and `force_update_` is **never read** anywhere, so it is a no-op.
- **Follow-ups (2242-2263):**
  - `SEQUENCE`: `if (!playmode || playmode >= PM_CV1 || selected.get_current_sequence() == seq || selected.update_timeout())` → `set_display_num_sequence(seq); pattern_changed(get_mask(seq), true);` (2246-2252). `update_timeout()` = `subticks_ > 30000` (~1.5 s with no clock, 861-864). This only updates what is displayed and, in ARP, the arpeggiator. The engine picks up the new sequence on its own (below).
  - `MODE`, `SEQUENCE_PLAYMODE`, `SEQUENCE_DIRECTION`: `update_enabled_settings` + `cursor.AdjustEnd(num_enabled_settings() - 1)` (2255-2260).
  - Nothing else. In particular there is nothing for `SEQUENCE_ARP_DIRECTION` and nothing for leaving edit mode.
- **On R push:** only the `SCALE` row does work on entering or leaving edit mode: `update_scale(true)` (2332), run on both the press that enters and the press that leaves edit mode.

**Does the ISR pick up a raw `apply_value` on its own?** (`SEQ_isr` 2132-2158 → `Update` 868 → `process_num_seq_channel` 1271 → `_clock` 1499)
- Read fresh every tick or every step, so they apply automatically: `MODE`, `CLOCK`, `TRIGGER_DELAY`, `RESET`, `MULT` (via `prev_multiplier_` 928), `PULSEWIDTH` (via `prev_pulsewidth_` 1231), `OCTAVE`, `ROOT`, `OCTAVE_AUX`, `SEQUENCE_PLAYMODE` (via `prev_playmode_`), `SEQUENCE_DIRECTION`, `SEQUENCE_PLAYMODE_CV_RANGES` (via `prev_input_range_`), the ARP dir/range, `BROWNIAN_PROBABILITY`, all `*_CV_SOURCE`, all env settings, and `SEQUENCE_LEN1..4`. ARP and S+H/CV modes detect a length change by comparing with `active_sequence_length_` (1325, 1406, 1431).
- `SCALE`: automatic. `update_scale(force_scale_update_)` runs every tick and reconfigures when `last_scale_ != scale` (840-855).
- **`SCALE_MASK`: NOT automatic.** `update_scale` only reconfigures the quantizer when `force` is set or the *scale* changed. The mask is reread only on `force_scale_update_`, which `update_scale_mask()` (756-759) and `scale_changed()` (752-754) set, or when a mask-rotate CV source is set (`rotate_scale` compares the rotated mask with `last_scale_mask_`, 1058-1061, 820-838). A raw write also leaves the SCALE_MASK row's drawing stale, because the row draws `get_rotated_scale_mask()` = `last_scale_mask_` (2461). Correct path: `update_scale_mask(mask, 0)` or `apply_value` + `scale_changed()`.
- `MASK1..4`: the active sequence's mask is reread at every step (`display_mask_ = get_mask(_num_seq)` 1474, used 1485). **In `PM_ARP`, though,** the note stack is rebuilt only when the sequence, length or playmode changes (1325), so a mask change needs `pattern_changed(mask, force)` (687-698, which calls `arpeggiator_.UpdateArpeggiator`). The menu's mask drawing (`get_display_mask()`, 2472) updates only on the next step or on `pattern_changed(..., true)`.
- `SEQUENCE`: the engine detects `_num_seq != sequence_manual_` (1286-1296) and resets, or waits for the end of the sequence if `wait_for_EoS_` is set. `display_num_sequence_` / `display_mask_` (title bar "/n", mask row) are updated by the next step or by the handler's follow-up.
- **`DUMMY`: NOT automatic.** The engine reads the member `wait_for_EoS_` (1288), which is copied from `values_[DUMMY]` only in `set_EoS_update()` (299-301, called from `SEQ_restore` 2104) and set together with it in `toggle_EoS()` (294-297). A raw write changes the saved value but not the behaviour until the next restore. The firmware also always toggles it on both channels together.
- The enabled list and cursor end are not rebuilt by a raw write; that only matters for the hardware menu.

### 6. Note masks and sub-editors

**Scale mask (`SCALE_MASK`, idx 10), `OC::ScaleEditor<SEQ_Channel>`** (`OC_scale_edit.h`; created with `scale_editor.Init(false)`, 2046, i.e. **not** Meta-Q `SEQ_MODE`):
- Opened by R push on the SCALE_MASK row when `scale != OC::Scales::SCALE_NONE` (2334-2341) → `Edit(owner, scale)` (`OC_scale_edit.h:49-67`). User scales 0..3 are editable (`mutable_scale_ = &g_user_scales[scale]`); built-in scales are read-only.
- Meaningful bits: the low `OC::Scales::GetScale(scale).num_notes` bits (`num_notes_ = scale_->num_notes`, 643). The menu draws it the same way (2461). The SCALE_MASK attr min is 1, and the editor refuses to clear the last set bit (`OC_scale_edit.h:566-568`). `invert_mask` never inverts to zero (583). `RotateMask` fills the bits above `num_notes` with 1s (605).
- Write-back: `apply_mask` → `owner_->update_scale_mask(mask_, edit_this_scale_)` (`OC_scale_edit.h:104-110`) → `force_scale_update_ = true; apply_value(SEQ_CHANNEL_SETTING_SCALE_MASK, mask)` (756-759). This channel only.
- Editor controls (non-SEQ_MODE):
  - L encoder moves the cursor (436-464).
  - R encoder rotates the mask (400). On a user scale it also: with L held, edits note pitches in the global `g_user_scales` (368-372); at the end position, changes `num_notes` 4..16 (375-396).
  - L push toggles the bit (547-576).
  - UP/DOWN push invert the mask (476-477, 514-515). With L held they change a note by ±128 or reset the scale to SEMI (469-474, 510-512).
  - R push closes (286-292).
  - L long opens the `_SCALING` page (301-305). That page edits the **global DAC voltage scaling** per DAC channel (`OC::DAC::set_scaling`, 448-453); R encoder picks the channel (418-424).
  - User-scale note or length edits call `owner_->scale_changed()` (431-432, 634-635).

**Step masks / sequences (`MASK1..4`, `SEQUENCE_LEN1..4`, pitches), `OC::PatternEditor<SEQ_Channel>`** (`OC_sequence_edit.h`):
- Opened by R push on the MASKn row (either page) → `pattern_editor.Edit(&selected, selected.get_sequence())` (2342-2352). `BeginEditing` sets `edit_this_sequence_ = get_sequence()`, `num_slots_ = get_sequence_length(seq)`, `mask_ = get_mask(seq)` (`OC_sequence_edit.h:408-415`).
- Meaningful bits: the low `get_sequence_length(seq)` bits (2..16). The menu row draws `get_display_mask()` with `get_display_length()` (2472). Bits above the length are kept and not cleared (except the L-held expand case below). An all-zero mask is allowed (attr min 0; "allow 0 mask", 356).
- Editor controls:
  - L encoder moves the cursor over the steps plus a "length" slot (316-322).
  - R encoder on a step: pitch ±1 semitone (`delta << 7`), or ±1/128 with L held. Clamped to `-3*(12<<7) .. 5*(12<<7)-128` (294-307), written by `owner_->set_pitch_at_step(edit_this_sequence_, cursor_pos_, pitch)` (631-637) into `g_user_patterns[seq + channel_offset]`.
  - R encoder on the length slot: length 2..16 → `owner_->set_sequence_length(num_slots_, edit_this_sequence_)` (583-601 → `SEQUENCE_LENn`) (269-288). With L held while expanding, it also clears mask bits from the cursor up and writes pitch 0 at index `num_slots_` (the new length, 280-281). At length 16 that is `notes[16]`, one past the array. This looks like an upstream off-by-one; behaviour beyond what the code shows is unverified.
  - L push toggles the step bit (351-363).
  - UP / DOWN push switch `edit_this_sequence_` to the next / previous of the 4 sequences ("offline" editing; does not change the `SEQUENCE` setting) (325-348).
  - L long: copy the sequence (`copy_seq`, 649-656, global copy buffer, `COPYTIMEOUT` = 200000 ISR ticks). L long with DOWN held, or DOWN long with L held: `clear_mask` (mask 0 + pitches reset to 0, 373-378). DOWN long: paste (`paste_seq`, 658-677: copies length, mask and the 16 notes into this sequence; works across channels because the source index is global; only within the timeout).
  - R push closes (222-224).
- Write-back: `apply_mask(mask)` runs after **every** encoder event (312) and after toggles. It calls `owner_->update_pattern_mask(mask_, edit_this_sequence_)` → `apply_value(MASKn)` (545-562), but only if the mask changed. It then **always** calls `owner_->pattern_changed(mask, force = (get_current_sequence() == edit_this_sequence_))` (74-83). That refreshes the menu display mask and, in ARP mode, rebuilds the arpeggiator. A friendly-mode pitch or mask edit should do the same. Everything is per channel and per sequence (`edit_this_sequence_`); pitches go to the global pattern table.

### 7. Title bar (SEQ_menu 2405-2437)

- `menu::DualTitleBar::Draw()`; per channel column `i` (`SetColumn(i)`):
  - Gate/step indicator `DrawGateIndicator(i, gate)`, with `gate = 1`, `+14` if `get_step_gate() == ON`, else `+10` if `get_step_state() == ON` (2415-2420).
  - Text `"#A/n"` / `"#B/n"`, where `n = 1 + get_display_num_sequence()` (the sequence currently playing, CV included) (2422-2428).
  - `"+"` at x+22 if `poke_octave_toggle()`, i.e. the UP octave toggle is engaged (2430-2432).
- `DualTitleBar::Selected(seq_state.selected_channel)` highlights the selected channel (2437).
- Live rows: the MASKn row draws the playing mask with a moving step cursor (`clock_indicator = get_clock_cnt()`, or 0xFF in ARP) (2468-2473). The SCALE_MASK row draws the effective, CV-rotated mask (2461). The editors draw over the list when open (2492-2495).

### 8. Screensaver and app events

- `SEQ_screensaver()` (2631-2635) → `RenderScreensaver()` for each channel (2499-2629). The screen is split at x=64 (`drawVLine(64, 0, 68)`). Each channel draws a step marker at `x = (ch<<6) + (clk_cnt<<2)` (in ARP a fixed position `6 + ch*4`): a 5×2 rect at y 63 when `step_state_ == OFF` (and the pitch is treated as 0), or 5×5 at y 60 when on. The current output pitch `get_step_pitch()` is drawn as a centred square (positive) or frame (negative), size `|pitch|>>6`, clamped to 40, with overflow squares above 40 and 80. Drawing only; no state changes.
- `SEQ_handleAppEvent` (2110-2127):
  - `APP_EVENT_RESUME` → `cursor.set_editing(false)`, `pattern_editor.Close()`, `scale_editor.Close()`.
  - `APP_EVENT_SUSPEND`, `APP_EVENT_SCREENSAVER_ON`, `APP_EVENT_SCREENSAVER_OFF` → for the **selected** channel only: `set_menu_page(PARAMETERS)` + `update_enabled_settings` (no `AdjustEnd`).
  - These change only UI state (page, editing, editors). No settings change.
- In Shoal, SCREENSAVER_ON/OFF are never sent (`o_c_REV.ino:151`). SUSPEND is sent when the R-long app menu opens (`OC_apps.ino:450`) and on host app switch (`src/shoal/CoreImpl.inc:313`). RESUME is sent on app-menu close (`OC_apps.ino:484`), on switch (`CoreImpl.inc:318`) and on init (`OC_apps.ino:401`).

### SettingsAccess.inc mapping notes (`src/shoal/SettingsAccess.inc:41,56`)

- `Entry<SEQ_Channel, SEQ_CHANNEL_SETTING_LAST, 2, &seqObj>` is correct as far as it goes: 2 objects, 56 settings each, `seq_channel[ch]`.
- **Missing (outside the settings table):**
  - The 16-step pitch data per sequence: `OC::g_user_patterns[seq + 4*ch]`, 8 slots × 16 `int16_t`. It is global settings storage, not app storage.
  - The user scales `g_user_scales[0..3]` (shared by all apps), edited by the scale editor.
  - The global DAC voltage scaling (scale editor's scaling page).
- **Raw `set` (plain `apply_value`) does not match the encoder result for:**
  - `SCALE_MASK`: has no effect until `force_scale_update_` is set. Use `update_scale_mask()` or `scale_changed()`.
  - `DUMMY` / EoS flag: has no effect until restore (`set_EoS_update()` needed), and the hardware toggles both channels together.
  - `MASKn` in ARP mode: needs `pattern_changed()`.
  - `SEQUENCE`: the display needs the 2249-2252 follow-up.
  - `MODE` / `SEQUENCE_PLAYMODE` / `SEQUENCE_DIRECTION`: need `update_enabled_settings` + `AdjustEnd` to keep the hardware menu consistent.
- **Settings the hardware edits only through sub-editors or buttons, never as rows:** `SEQUENCE_LEN1..4`, the masks of sequences that are not selected, `DUMMY`. Duplicate name strings (10-14, 16-19, 21/23, 34/36) need disambiguating labels. `OCTAVE` is also changed by the UP button together with the unsaved `octave_toggle_` flag, which a settings-only view cannot represent.

## Dialectic Ping Pong (APP_BBGEN.ino)

Registered at OC_apps.ino:64.

### 1. Channels
- There are four settings objects, `bbgen.balls_[0..3]` (`BouncingBall balls_[4]`, 275). The container is `QuadBouncingBalls bbgen` (283).
- The UI calls them A to D (QuadTitleBar, 326-330).
- **L encoder** selects the channel (371-374): `CONSTRAIN(selected_channel + value, 0, 3)`.
- SettingsAccess.inc maps this correctly: 4 channels, `bbgen.balls_[ch]`, 11 settings.

### 2. Settings table (enum 36-49, `SETTINGS_DECLARE` 207-219)

| idx | enum | name | min | max | default | value names |
|---|---|---|---|---|---|---|
| 0 | BB_SETTING_GRAVITY | "Gravity" | 0 | 255 | 128 | NULL |
| 1 | BB_SETTING_BOUNCE_LOSS | "Bounce loss" | 0 | 255 | 96 | NULL |
| 2 | BB_SETTING_INITIAL_AMPLITUDE | "Amplitude" | 0 | 255 | 0 | NULL |
| 3 | BB_SETTING_INITIAL_VELOCITY | "Velocity" | 0 | 255 | 228 | NULL |
| 4 | BB_SETTING_TRIGGER_INPUT | "Trigger input" | 0 | 3 | 0 in the table, but `Init` applies the channel index, so the defaults are TR1..TR4 for A..D (198, 228-233) | `OC::Strings::trigger_input_names`: "TR1","TR2","TR3","TR4" |
| 5 | BB_SETTING_RETRIGGER_BOUNCES | "Retrigger" | 0 | 255 | 0 | NULL |
| 6 | BB_SETTING_CV1 | "CV1 -> " | 0 | 5 | 0 | `bb_cv_mapping_names` (203-205): "off","grav","bnce","ampl","vel","retr" |
| 7 | BB_SETTING_CV2 | "CV2 -> " | 0 | 5 | 0 | `bb_cv_mapping_names` |
| 8 | BB_SETTING_CV3 | "CV3 -> " | 0 | 5 | 0 | `bb_cv_mapping_names` |
| 9 | BB_SETTING_CV4 | "CV4 -> " | 0 | 5 | 0 | `bb_cv_mapping_names` |
| 10 | BB_SETTING_HARD_RESET | "Hard reset" | 0 | 1 | 0 | `OC::Strings::no_yes`: "No","Yes" |

### 3. Visible settings
- The app has no enabled-settings list and no pages. The cursor is `ui.cursor.Init(BB_SETTING_GRAVITY, BB_SETTING_LAST - 1)` (239), so all 11 settings are always shown.
- The list shows the selected channel's values (`bbgen.selected()`, 332-338).
- `ui.left_edit_mode`, `ui.left_encoder_value` and `ui.selected_segment` are initialized (235-238) but never changed. They are leftovers from the Piqued code.

### 4. Controls (`BBGEN_handleButtonEvent` 351-367, `BBGEN_handleEncoderEvent` 369-383)
- **L encoder** selects the channel through `ui.selected_channel`, a UI member.
- **R encoder** edits `change_value(cursor_pos(), value)` on the selected ball while editing (376-378), which edits a setting. Otherwise it scrolls.
- **L push** does nothing (360-361).
- **R push** does `cursor.toggle_editing()` (362-363).
- **UP** (`BBGEN_topButton` 341-344) applies `change_value(BB_SETTING_GRAVITY + ui.selected_segment, 32)`. `selected_segment` is always 0, so this is always **Gravity +32** on the selected channel. **DOWN** (346-349) applies Gravity -32. These are encoder-only shortcuts to setting 0, which is also a menu row.
- **Long presses:** none are handled by the app. UP long is a no-op in Shoal, R long opens the app menu (OC_ui.cpp:168-170), and L long and DOWN long are no-ops.

### 5. Guards and follow-ups
- There are none beyond the clamp, and nothing happens on leaving edit mode.
- `BouncingBall::Update` (147-186) reads every setting each ISR tick (`QuadBouncingBalls::ISR` 242-255): it calls `bb_.Configure(s)` and `set_hard_reset`, and reads the trigger input each tick. A raw `apply_value` therefore takes effect exactly as an encoder edit would.

### 6. Masks and sub-editors
None.

### 7. Title bar (`BBGEN_menu` 323-339)
`QuadTitleBar` with the letters A to D and `Selected(selected_channel)`. There are no gate indicators or live values.

### 8. Screensaver
- `BBGEN_screensaver` calls `OC::scope_render()` (400-402).
- `BBGEN_handleAppEvent` (308-318): RESUME calls `ui.cursor.set_editing(false)`. The other events do nothing.

## Viznutcracker sweet (`APP_BYTEBEATGEN.ino`)

All paths relative to `src/shoal/firmware/`. Build options in effect (`OC_options.h:10-43`): no `VOR`, `BUCHLA_4U`, `BUCHLA_SUPPORT`, `FLIP_180`.

### 1. Channels
- 4 independent `ByteBeat` settings objects: `QuadByteBeats::bytebeats_[4]` (`APP_BYTEBEATGEN.ino:435`), global instance `bytebeatgen` (`:443-444`). Each drives one DAC output: channel 0 → A … 3 → D (`:410-413`).
- UI calls them **A, B, C, D**: a `QuadTitleBar` with the letters 'A'+i, the selected one highlighted (`BYTEBEATGEN_menu`, `:489-494`).
- Selected with the **L encoder**: `selected_channel + event.value`, `CONSTRAIN(left_value, 0, 3)`, then `cursor.AdjustEnd(selected.num_enabled_settings() - 1)` (`:539-544`). No wrap. The cursor position is not reset.
- `ui.left_edit_mode`, `ui.left_encoder_value` and `ui.selected_segment` (`:421-429`) are set in `Init` (`:392-396`) and never changed afterwards. `selected_segment` stays 0.

### 2. Settings table (`SETTINGS_DECLARE`, `:356-376`; enum `:36-58`)

| idx | enum | name | min | max | default | value names |
|---|---|---|---|---|---|---|
| 0 | `BYTEBEAT_SETTING_EQUATION` | "Equation" | 0 | 15 | 0 | `OC::Strings::bytebeat_equation_names` (`OC_strings.cpp:43-45`): "hope","love","life","age","clysm","monk","NERV","Trurl","Pirx","Snaut","Hari","Kris","Tichy","Bregg","Avon","Orac" |
| 1 | `BYTEBEAT_SETTING_SPEED` | "Speed" | 0 | 255 | 255 | numeric |
| 2 | `BYTEBEAT_SETTING_PITCH` | "Pitch" | 1 | 255 | 1 | numeric |
| 3 | `BYTEBEAT_SETTING_P0` | "Parameter 0" | 0 | 255 | 126 | numeric |
| 4 | `BYTEBEAT_SETTING_P1` | "Parameter 1" | 0 | 255 | 126 | numeric |
| 5 | `BYTEBEAT_SETTING_P2` | "Parameter 2" | 0 | 255 | 127 | numeric |
| 6 | `BYTEBEAT_SETTING_LOOP_MODE` | "Loop mode" | 0 | 1 | 0 | `OC::Strings::no_yes`: "No","Yes" |
| 7 | `BYTEBEAT_SETTING_LOOP_START` | "Loop begin ++" | 0 | 255 | 0 | numeric |
| 8 | `BYTEBEAT_SETTING_LOOP_START_MED` | "Loop begin +" | 0 | 255 | 0 | numeric |
| 9 | `BYTEBEAT_SETTING_LOOP_START_FINE` | "Loop begin" | 0 | 255 | 0 | numeric |
| 10 | `BYTEBEAT_SETTING_LOOP_END` | "Loop end ++" | 0 | 255 | 0 | numeric |
| 11 | `BYTEBEAT_SETTING_LOOP_END_MED` | "Loop end +" | 0 | 255 | 1 | numeric |
| 12 | `BYTEBEAT_SETTING_LOOP_END_FINE` | "Loop end" | 0 | 255 | 255 | numeric |
| 13 | `BYTEBEAT_SETTING_TRIGGER_INPUT` | "Trigger input" | 0 (`DIGITAL_INPUT_1`) | 3 (`DIGITAL_INPUT_4`) | 0 in the table, **but `Init` sets channel i to TR(i+1)** (see note) | `OC::Strings::trigger_input_names`: "TR1","TR2","TR3","TR4" |
| 14 | `BYTEBEAT_SETTING_STEP_MODE` | "Step mode" | 0 | 1 | 0 | `no_yes`: "No","Yes" |
| 15 | `BYTEBEAT_SETTING_CV1` | "CV1 -> " | 0 | 12 | 0 | `bytebeat_cv_mapping_names` (`:352-354`): "off","equ","spd","p0","p1","p2","beg++","beg+","beg","end++","end+","end","pitch" |
| 16 | `BYTEBEAT_SETTING_CV2` | "CV2 -> " | 0 | 12 | 0 | same |
| 17 | `BYTEBEAT_SETTING_CV3` | "CV3 -> " | 0 | 12 | 0 | same |
| 18 | `BYTEBEAT_SETTING_CV4` | "CV4 -> " | 0 | 12 | 0 | same |

`BYTEBEAT_SETTING_LAST` = 19.

Note on defaults: `ByteBeat::Init` runs `InitDefaults()` and then `apply_value(BYTEBEAT_SETTING_TRIGGER_INPUT, default_trigger)` (`:343-345`). `QuadByteBeats::Init` passes TR1..TR4 to channels 0..3 (`:385-390`). A "reset to default" therefore gives channel i trigger TR(i+1), not the table's TR1.

### 3. Visible settings
- Enabled-settings list: `num_enabled_settings()` / `enabled_setting_at()` / `update_enabled_settings()` (`:196-229`).
- Order: EQUATION, SPEED, PITCH, P0, P1, P2, LOOP_MODE, then `if (get_loop_mode())` LOOP_START, LOOP_START_MED, LOOP_START_FINE, LOOP_END, LOOP_END_MED, LOOP_END_FINE (`:213-220`), then TRIGGER_INPUT, STEP_MODE, CV1..CV4. That is 13 rows with loop mode off and 19 with it on.
- The six loop rows are indented (`indentSetting`, `:231-243`, applied at `:503-504`).
- One page, no page switch. Every setting can appear. The loop rows are only hidden.
- Quirk: `ScreenCursor::AdjustEnd` (`OC_menus.h:83-86`) does not clamp `cursor_pos_`. Suppose the cursor is at row 13-18 of a channel with loop mode on, and the L encoder then selects a channel with loop mode off. The cursor is now past the end, and `enabled_setting_at()` reads stale entries of `enabled_settings_` until the user scrolls. This is an upstream bug.
- `BYTEBEATGEN_restore` (`:461-469`) calls `AdjustEnd` from `bytebeats_[0]`, whatever channel is selected.

### 4. Controls (`BYTEBEATGEN_handleButtonEvent` `:519-535`, `BYTEBEATGEN_handleEncoderEvent` `:537-561`)
- **L encoder**: selects the channel (`ui.selected_channel`, a member variable, not a setting). `:539-544`.
- **R encoder**: if `cursor.editing()`, `change_value(setting, event.value)` on the selected channel's setting at the cursor (`:546-549`). Otherwise it scrolls the cursor (`:558`).
- **L push**: nothing (`:528-529`).
- **R push**: `cursor.toggle_editing()` (`:530-531`).
- **UP push**: `BYTEBEATGEN_topButton` runs `change_value(BYTEBEAT_SETTING_EQUATION + selected_segment, 1)` (`:509-512`). `selected_segment` is always 0, so this is **Equation +1** on the selected channel, whether or not edit mode is on. It is an encoder-only shortcut, but it changes the setting `BYTEBEAT_SETTING_EQUATION`.
- **DOWN push**: `BYTEBEATGEN_lowerButton`, **Equation −1** (`:514-517`). Same notes as UP.
- **Long presses**: R long opens the global app menu (`OC_ui.cpp:168-170`). In Shoal, UP long does nothing and is not forwarded (`OC_ui.cpp:159-167`). L and DOWN long are forwarded (`OC_ui.cpp:172`), but the handler only reacts to `EVENT_BUTTON_PRESS` (`:520`), so they do nothing.
- Non-row functions: none besides UP/DOWN Equation ±1, which only touches a table setting.

### 5. Guards and follow-ups
- Before an edit: no refusals. `change_value` clamps to [min, max] (`util/util_settings.h:86-99`).
- After an edit: `case BYTEBEAT_SETTING_LOOP_MODE: selected.update_enabled_settings(); bytebeatgen.ui.cursor.AdjustEnd(selected.num_enabled_settings() - 1);` (`:550-556`). Nothing else.
- UP/DOWN Equation changes need no follow-up.
- Nothing is deferred to leaving edit mode or to a button press.
- Raw `apply_value`: `ByteBeat::Update` reads every setting on every ISR tick (`:273-299`, called from `QuadByteBeats::ISR` `:401-414`), so values take effect immediately. The one follow-up a raw write must copy is the `update_enabled_settings()` + `AdjustEnd` after `LOOP_MODE`.

### 6. Note masks and sub-editors
None. No mask settings and no sub-editor.

### 7. Title bar
`QuadTitleBar` with "A".."D" and the selected channel highlighted (`:489-494`). No live indicators.

### 8. Screensaver
- `BYTEBEATGEN_screensaver` (`:580-599`) draws each channel's output-byte history (`ReadHistory`, 64 entries) as four mirrored 16-px bands, one per channel. It reads only. `bb_history` is draw scratch.
- `BYTEBEATGEN_handleAppEvent` (`:472-482`): on `APP_EVENT_RESUME`, `cursor.set_editing(false)`. On `SUSPEND`, `SCREENSAVER_ON` and `SCREENSAVER_OFF`, nothing.
- Shoal never sends the screensaver events (`o_c_REV.ino:151`). SUSPEND and RESUME are sent around the app menu (`OC_apps.ino:450,484`) and on app switch (`CoreImpl.inc:313-318`).

---

## Acid Curds (`APP_CHORDS.ino`, with `OC_chords.*`, `OC_chords_edit.h`, `OC_chords_presets.h`, `OC_scale_edit.h`)

### 1. Channels
- One `Chords` settings object: `SHOAL_STATE(Chords, chords)` (`APP_CHORDS.ino:1052-1053`). UI state lives separately in `ChordQuantizer chords_state` (`:1027-1051`): cursor, `scale_editor`, `chord_editor`, `left_encoder_value`.
- No channel tabs. Outputs A-D are the four chord voices of one quantizer (`:805-825`).
- The menu has **two pages** instead (see §3).
- Chord data lives **outside the settings object** in the global `OC::g_user_chords[32]` (`OC_chords.h:102-103`). That is 4 progressions × 8 chords: slot = `chord + progression * NUM_CHORDS`, with `NUM_CHORDS` = 32/4 = 8 (`OC_chords.cpp:9-12`, `OC_chords.h:18-53`).
- The chord array is persisted with **global settings**, not app storage: `save_global_settings` → `global_settings.user_chords` (`OC_apps.ino:88,197`), loaded at `OC_apps.ino:370`, sanitized at `OC_apps.ino:173-179`. Shoal's `Core::save` calls `save_global_settings()` (`CoreImpl.inc:371`).

### 2. Settings table (`SETTINGS_DECLARE`, `:992-1025`; enum `:43-77`)
Constants: `OC::Scales::SCALE_USER_0..3` = 0..3; `SCALE_NONE` = `SCALE_USER_LAST` = 4; `SCALE_SEMI` = 5 (`OC_scales.h:20-26`). `NUM_SCALES` = 4 + `sizeof(braids::scales)/sizeof(...)` (`OC_scales.cpp:12`), about 102 by my count of 98 braids scales. `OC::Chords::NUM_CHORD_PROGRESSIONS` = 4, `CHORDS_USER_LAST` = 32. `OC::kNumDelayTimes` = 8.

| idx | enum | name | min | max | default | value names |
|---|---|---|---|---|---|---|
| 0 | `CHORDS_SETTING_SCALE` | "scale" | 5 (`SCALE_SEMI`) | `NUM_SCALES-1` | 5 (`SCALE_SEMI`) | `OC::scale_names` (`OC_scales.cpp:169-`), about 102 scale names. Because min is SEMI, the user scales 0-3 and "Off " (4) cannot be selected. |
| 1 | `CHORDS_SETTING_ROOT` | "root" | 0 | 11 | 0 | `OC::Strings::note_names_unpadded`: "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" |
| 2 | `CHORDS_SETTING_PROGRESSION` | "progression" | 0 | 3 | 0 | `chords_slots` (`:984-986`): "#1".."#8" (only #1-#4 reachable) |
| 3 | `CHORDS_SETTING_MASK` | "scale  -->" | 1 | 65535 | 65535 | none (U16 bit mask, drawn as a mask) |
| 4 | `CHORDS_SETTING_CV_SOURCE` | "CV source" | 0 | 3 | 0 | `OC::Strings::cv_input_names`: "CV1","CV2","CV3","CV4" |
| 5 | `CHORDS_SETTING_CHORDS_ADVANCE_TRIGGER_SOURCE` | "chords trg src" | 0 | 1 | 1 (`..._TR2`) | `chords_advance_trigger_sources` (`:980-982`): "TR1","TR2" |
| 6 | `CHORDS_SETTING_PLAYMODES` | "playmode" | 0 | 14 | 0 | `chord_playmodes` (`:988-990`): "-","SEQ+1","SEQ+2","SEQ+3","TR3+1","TR3+2","TR3+3","S+H#1","S+H#2","S+H#3","S+H#4","CV#1","CV#2","CV#3","CV#4" |
| 7 | `CHORDS_SETTING_DIRECTION` | "direction" | 0 | 5 | 0 | `OC::Strings::seq_directions`: "fwd","rev","pnd1","pnd2","rnd","brwn" |
| 8 | `CHORDS_SETTING_BROWNIAN_PROBABILITY` | "-->brown prob" | 0 | 255 | 64 | numeric |
| 9 | `CHORDS_SETTING_TRIGGER_DELAY` | "TR1 delay" | 0 | 7 | 0 | `OC::Strings::trigger_delay_times`: "off","120us","240us","360us","480us","1ms","2ms","4ms" |
| 10 | `CHORDS_SETTING_TRANSPOSE` | "transpose" | -5 | 7 | 0 | numeric (I8) |
| 11 | `CHORDS_SETTING_OCTAVE` | "octave" | -4 | 4 | 0 | numeric (I8) |
| 12 | `CHORDS_SETTING_CHORD_SLOT` | "chord:" | 0 | 31 | 0 | `chords_slots` "#1".."#8". **Values past 7 index past the 8-entry array.** |
| 13 | `CHORDS_SETTING_NUM_CHORDS_0` | "num.chords" | 0 | 31 | 0 | numeric (length − 1 of progression 1) |
| 14 | `CHORDS_SETTING_NUM_CHORDS_1` | "num.chords" | 0 | 31 | 0 | progression 2 |
| 15 | `CHORDS_SETTING_NUM_CHORDS_2` | "num.chords" | 0 | 31 | 0 | progression 3 |
| 16 | `CHORDS_SETTING_NUM_CHORDS_3` | "num.chords" | 0 | 31 | 0 | progression 4 |
| 17 | `CHORDS_SETTING_CHORD_EDIT` | "chords -->" | 0 | 0 | 0 | none (action row: chord editor) |
| 18 | `CHORDS_SETTING_ROOT_CV` | "root CV      >" | 0 | 4 | 0 | `OC::Strings::cv_input_names_none`: " - ","CV1","CV2","CV3","CV4" |
| 19 | `CHORDS_SETTING_MASK_CV` | "mask CV      >" | 0 | 4 | 0 | same |
| 20 | `CHORDS_SETTING_TRANSPOSE_CV` | "transpose CV >" | 0 | 4 | 0 | same |
| 21 | `CHORDS_SETTING_OCTAVE_CV` | "octave CV    >" | 0 | 4 | 0 | same |
| 22 | `CHORDS_SETTING_QUALITY_CV` | "quality CV   >" | 0 | 4 | 0 | same |
| 23 | `CHORDS_SETTING_VOICING_CV` | "voicing CV   >" | 0 | 4 | 0 | same |
| 24 | `CHORDS_SETTING_INVERSION_CV` | "inversion CV >" | 0 | 4 | 0 | same |
| 25 | `CHORDS_SETTING_PROGRESSION_CV` | "prg.slot# CV >" | 0 | 4 | 0 | same |
| 26 | `CHORDS_SETTING_DIRECTION_CV` | "direction CV >" | 0 | 4 | 0 | same |
| 27 | `CHORDS_SETTING_BROWNIAN_CV` | "-->br.prb CV >" | 0 | 4 | 0 | same |
| 28 | `CHORDS_SETTING_NUM_CHORDS_CV` | "num.chrds CV >" | 0 | 4 | 0 | same |
| 29 | `CHORDS_SETTING_DUMMY` | "-" | 0 | 0 | 0 | none |
| 30 | `CHORDS_SETTING_MORE_DUMMY` | " " | 0 | 0 | 0 | none |

`CHORDS_SETTING_LAST` = 31.

Encoder-reachable ranges differ from the table:
- `NUM_CHORDS_0..3` are only set by the chord editor, which clamps to `0..NUM_CHORDS-1` = 0..7 (`OC_chords_edit.h:347-353`). The table allows 0..31.
- `CHORD_SLOT` is never in the menu (see §3).
- `Init` calls `set_scale(SCALE_SEMI)` (`:376`), which leaves the default mask 65535 as is.

**Chord-level structure** (not a settings object): `struct OC::Chord { int8_t quality; int8_t inversion; int8_t voicing; int8_t base_note; int8_t octave; }` (`OC_chords_presets.h:10-17`). The default for all 32 slots is `{0,0,0,0,0}` (`OC_chords_presets.h:19-22`, `OC_chords.cpp:16-19`). The chord editor's own column order is **Q, V, I, B, O** (`OC_strings.cpp:59` `chord_property_names`), not the struct's field order.

| field (editor column) | range (editor clamp) | names |
|---|---|---|
| quality (Q, col 0) | 0..8 (`CHORDS_QUALITY_LAST-1`) `OC_chords_edit.h:307-309` | `quality_names`: "fifth","triad","seventh","suspended","susp 7th","sixth","added 9th","added 11th","unisono" (short forms at `OC_chords_presets.h:60-66`) |
| voicing (V, col 1) | 0..4 `:314-316` | `voicing_names`: "close","drop 1","drop 2","drop 3","spread" |
| inversion (I, col 2) | 0..3 `:321-323` | `inversion_names`: "-","1","2","3" |
| base_note (B, col 3) | 0..`GetScale(scale).num_notes` `:328-331` | `base_note_names`: "CV","#1".."#16". 0 means "follow CV pitch". Update limits it again to the scale length (`APP_CHORDS.ino:750-758`). |
| octave (O, col 4) | -4..4 `:336-338` | numeric |

### 3. Visible settings
- Enabled-settings list: `num_enabled_settings` / `enabled_setting_at` / `update_enabled_settings` (`:861-927`). The list depends on `menu_page_`, a member variable (`:945`) and not a setting. It is not saved: `Init` sets it with `menu_page_ = PARAMETERS;` (`:374`); `PARAMETERS` is Sequins' enum value `APP_A_SEQ.ino:222`, which is 0, the same as `MENU_PARAMETERS`; the two only agree by accident.
- **Page `MENU_PARAMETERS`** (`:875-896`), in order:
  - `CHORDS_SETTING_MASK`
  - `CHORDS_SETTING_ROOT` `if (get_scale(DUMMY) != OC::Scales::SCALE_NONE)`, else `CHORDS_SETTING_MORE_DUMMY`
  - `CHORDS_SETTING_PROGRESSION`
  - `CHORDS_SETTING_CHORD_EDIT`
  - `CHORDS_SETTING_PLAYMODES`
  - `CHORDS_SETTING_DIRECTION` `if (get_playmode() < _SH1)`
  - `CHORDS_SETTING_BROWNIAN_PROBABILITY` `if (get_direction() == CHORDS_BROWNIAN)`. This does not depend on playmode, so it can show while DIRECTION is hidden.
  - `CHORDS_SETTING_TRANSPOSE`, `CHORDS_SETTING_OCTAVE`, `CHORDS_SETTING_CV_SOURCE`, `CHORDS_SETTING_CHORDS_ADVANCE_TRIGGER_SOURCE`, `CHORDS_SETTING_TRIGGER_DELAY`
- **Page `MENU_CV_MAPPING`** (`:898-920`), in order:
  - `CHORDS_SETTING_MASK_CV`
  - `CHORDS_SETTING_ROOT_CV` under the same SCALE_NONE condition, else `MORE_DUMMY`
  - `CHORDS_SETTING_PROGRESSION_CV`
  - `CHORDS_SETTING_CHORD_EDIT` (on both pages)
  - `CHORDS_SETTING_NUM_CHORDS_CV`
  - `CHORDS_SETTING_DIRECTION_CV` `if (get_playmode() < _SH1)`
  - `CHORDS_SETTING_BROWNIAN_CV` `if (get_direction() == CHORDS_BROWNIAN)`
  - `TRANSPOSE_CV`, `OCTAVE_CV`, `QUALITY_CV`, `INVERSION_CV`, `VOICING_CV`
- Both pages have 10-12 rows, the same count for the same playmode and direction.
- **Page switching**: DOWN push toggles PARAMETERS ↔ CV_MAPPING (`CHORDS_lowerButton` `:1280-1301`). UP push on the CV page returns to PARAMETERS (`:1273-1277`). R push on DUMMY or MORE_DUMMY sets PARAMETERS (`:1316-1320`). Each switch calls `update_enabled_settings()` and, except for the R-push case, `cursor.set_editing(false)`. **None of them call `cursor.AdjustEnd`**, and the cursor position carries over between pages.
- **Never shown**:
  - `CHORDS_SETTING_SCALE`: set with the L encoder + L push, and shown in the title bar.
  - `CHORDS_SETTING_CHORD_SLOT`: no path sets it from the UI, so it stays 0 unless restored or raw-written. It is only read as the chord editor's starting cursor (`OC_chords_edit.h:444`). The `CHORD_SLOT` clamp at `:1243-1247` and `DrawValueMax` at `:1152-1154` are unreachable from the menu.
  - `CHORDS_SETTING_NUM_CHORDS_0..3`: edited inside the chord editor.
  - `CHORDS_SETTING_DUMMY`
  - `CHORDS_SETTING_MORE_DUMMY` in practice: it only appears when scale == `SCALE_NONE`, which the scale range (min `SCALE_SEMI`) and the L encoder clamp (`:1228`) rule out. ROOT and ROOT_CV are therefore always shown.
- Row drawing (`CHORDS_menu` `:1131-1159`):
  - MASK: drawn as a mask of `get_rotated_mask()` with `num_notes` bits, no value.
  - CHORD_EDIT (and DUMMY): drawn as a mini chord strip of `get_display_num_chords()` with `active_chord()` highlighted.
  - MORE_DUMMY: no value.

### 4. Controls (`CHORDS_handleButtonEvent` `:1168-1212`, `CHORDS_handleEncoderEvent` `:1214-1262`)
Long presses are dispatched first (`:1170-1185`), then go to an open editor (`:1187-1194`). Short presses and encoders go to the open editor if there is one; otherwise:
- **L encoder**: changes `chords_state.left_encoder_value`, a scale preview clamped to `SCALE_SEMI..NUM_SCALES-1` (`:1225-1229`). It is a member variable, not a setting. The setting is unchanged until L push.
- **L push**: `CHORDS_leftButton` (`:1327-1334`). The condition is `if (chords_state.left_encoder_value != chords.get_scale(DUMMY) || chords_state.left_encoder_value == OC::Scales::SCALE_SEMI)`. When it holds, it runs `chords.set_scale(left_encoder_value)` and then `update_enabled_settings()`. This changes the setting `CHORDS_SETTING_SCALE` and possibly `CHORDS_SETTING_MASK` (see §5).
- **R encoder**: when editing, `change_value` on the row's setting, except MASK (§5). Otherwise it scrolls.
- **R push**: `CHORDS_rightButton` (`:1303-1325`).
  - On MASK: opens the scale editor, `if (OC::Scales::SCALE_NONE != scale)`.
  - On CHORD_EDIT: opens the chord editor with `(get_chord_slot(), get_num_chords(get_progression()), get_progression())`.
  - On DUMMY or MORE_DUMMY: switches to page PARAMETERS.
  - Otherwise: `cursor.toggle_editing()`. MASK and CHORD_EDIT never enter edit mode.
- **UP push**: `CHORDS_topButton` (`:1264-1278`).
  - On page PARAMETERS: `if (chords.octave_toggle()) change_value(CHORDS_SETTING_OCTAVE, 1); else change_value(CHORDS_SETTING_OCTAVE, -1);`. `octave_toggle()` flips the member `_octave_toggle` (`:171-174`). Successive UP presses alternate octave +1, −1, +1 …: an encoder-only "octave jump" that changes the setting `CHORDS_SETTING_OCTAVE` plus the member `_octave_toggle`, which the title bar shows ("+" vs "#").
  - On page CV: switches to PARAMETERS, editing off.
- **DOWN push**: toggles the menu page. This is the member `menu_page_`, not a setting.
- **L long**: `if (!(chords_state.chord_editor.active())) CHORDS_leftButtonLong();`, which is empty (`:1336-1338`). The event is then also passed to an open scale editor, where L long switches to its DAC scaling page (§6).
- **DOWN long**: `CHORDS_downButtonLong` (`:1340-1343`): `clear_CV_mapping()` sets all eleven `*_CV` settings (ROOT, MASK, TRANSPOSE, OCTAVE, QUALITY, VOICING, INVERSION, BROWNIAN, DIRECTION, PROGRESSION, NUM_CHORDS) to 0 (`:356-369`) and `cursor.set_editing(false)`. **This runs even while an editor is open**, and the event is then forwarded to that editor.
- **UP long**: `CHORDS_upButtonLong` is empty (`:1345-1347`). In Shoal, `OC_ui.cpp:159-167` consumes UP long and never forwards it anyway.
- **R long**: the global app menu (`OC_ui.cpp:168-170`). It sends SUSPEND; closing sends RESUME, which closes both editors (§8).

Encoder-only functions (not menu rows):
- scale preview + commit (L encoder + L push) → `CHORDS_SETTING_SCALE` (+ MASK fix-up)
- octave ±1 alternating (UP) → `CHORDS_SETTING_OCTAVE` + member `_octave_toggle`
- page toggle (DOWN, UP) → member `menu_page_`
- clear CV mapping (DOWN long) → 11 `*_CV` settings
- open scale editor (R push on MASK)
- open chord editor (R push on CHORD_EDIT) → writes the global `OC::g_user_chords` and `CHORDS_SETTING_NUM_CHORDS_0..3`

### 5. Guards and follow-ups (R encoder, `:1231-1257`)
- Refusal: `if (CHORDS_SETTING_MASK != setting)`. The R encoder never edits MASK from the list; MASK can only change through the scale editor or `set_scale`.
- `if (chords.change_value(setting, event.value)) chords.force_update();`. `force_update()` is a no-op: its body is commented out (`:404-406`).
- After the edit:
  - `case CHORDS_SETTING_CHORD_SLOT:` `if (chords.get_chord_slot() > chords.get_num_chords(chords.get_progression())) chords.set_chord_slot(chords.get_num_chords(chords.get_progression()));`. Unreachable from the menu.
  - `case CHORDS_SETTING_DIRECTION: case CHORDS_SETTING_PLAYMODES:` `update_enabled_settings(); cursor.AdjustEnd(num_enabled_settings() - 1);`
- Scale commit (`set_scale`, `:144-154`), run only on L push and on restore. It does nothing if the scale is unchanged. Otherwise, `if (0 == (mask & ~(0xffff << scale_def.num_notes))) mask |= 0x1;` then `apply_value(CHORDS_SETTING_MASK, mask)` and `apply_value(CHORDS_SETTING_SCALE, scale)`. After that, `update_enabled_settings()` (`:1332`). A friendly-mode scale dropdown must copy this mask fix-up.
- The ISR picks up settings, but only on a trigger. `Chords::Update` (`:547-840`) reads every setting inside `if (triggered)` (TR1 after `TRIGGER_DELAY`). `update_scale(force_update_, mask_rotate)` reconfigures the quantizer when scale or mask differ from `last_scale_`/`last_mask_` (`:960-977`). A changed `PROGRESSION` or `PLAYMODES` resets the progression counter (`:585-591`).
- So a raw `apply_value` takes effect on the next trigger, exactly as an encoder edit does. The exceptions are:
  1. the enabled-list rebuild for DIRECTION and PLAYMODES (+ `AdjustEnd`);
  2. the `set_scale` mask fix-up for SCALE;
  3. MASK written raw does not update `last_mask_`, so the mask display (`get_rotated_mask()`) lags until the next trigger. `update_scale_mask` (`:851-855`) also sets `last_mask_ = mask; force_update_ = true;`.
- Nothing is applied on leaving edit mode.

### 6. Note masks and sub-editors
**Scale mask** `CHORDS_SETTING_MASK` (U16, min 1):
- Meaningful bits: `OC::Scales::GetScale(chords.get_scale(DUMMY)).num_notes`. The menu draws it with that count (`:1140`).
- Upper bits are normally 1: default 65535, and `RotateMask` fills them (`OC_scale_edit.h:605`).
- Opened by R push on the "scale  -->" row: `scale_editor.Edit(&chords, scale)` (`:1307-1311`). The editor is `Init(false)`, i.e. not Meta-Q `SEQ_MODE` (`:1031`).
- Since the scale is always ≥ `SCALE_SEMI` (> `SCALE_USER_LAST`), `mutable_scale_ = nullptr` (`OC_scale_edit.h:58-61`). Only the mask can be edited, not notes or length.

Inside the scale editor:
- L encoder: moves the note cursor 0..num_notes−1 (`OC_scale_edit.h:456-461`).
- R encoder: rotates the mask, `RotateMask(mask_, num_notes_, event.value)` (`:399-401`).
- L push: toggles the bit at the cursor, refusing to clear the last set bit: `if ((mask & ~(0xffff << num_notes_)) != m) mask &= ~m;` (`:561-573`).
- UP or DOWN push (L not held): `invert_mask()`, which does not invert to zero (`:476-477, 514-515, 579-586`). The L-held variants call `change_note`/`reset_scale`, which are no-ops or unreachable without a mutable scale.
- R push: `Close()` (`:286-292`).
- L long: opens the **DAC voltage-scaling page** (`edit_page_ = _SCALING`, `:301-305`). There, R encoder picks a DAC channel and L encoder runs `OC::DAC::set_scaling(...)`, global output scaling per channel (`:448-453`, `:418-424`). Any other long press, or R push, returns to `_SCALE`.
- Write-back: `apply_mask` → `owner_->update_scale_mask(mask_, edit_this_scale_)` → `Chords::update_scale_mask`: `apply_value(CHORDS_SETTING_MASK, mask); last_mask_ = mask; force_update_ = true;` (`APP_CHORDS.ino:851-855`). One object.

**Chord editor** (`OC::ChordEditor<Chords>`, `OC_chords_edit.h`):
- Opened by R push on "chords -->": `Edit(&chords, get_chord_slot(), get_num_chords(get_progression()), get_progression())` (`APP_CHORDS.ino:1313-1314`; `OC_chords_edit.h:64-77`).
- The cursor starts at `get_chord_slot()`, normally 0. The editor's progression `edit_this_progression_` starts at the `PROGRESSION` setting.
- L encoder (`move_cursor`, `:371-387`): on the select page it moves across chords `0..max_chords_+1`, where the last position is the end marker, and loads that chord. On the edit page it moves across the 5 properties.
- R encoder (`:287-355`): if the cursor is on a chord, it changes property `cursor_quality_pos_` of chord `edit_this_chord_ + edit_this_progression_*8`, writing **directly into `OC::g_user_chords[...]`**. Writes past index 31 are dropped (SHOAL guard `:298-301`). This happens on the select page too. If the cursor is on the end marker, it changes the progression length (0..7) and writes it back with `owner_->set_num_chords(max_chords, edit_this_progression_)`, which sets `CHORDS_SETTING_NUM_CHORDS_<p>` (`APP_CHORDS.ino:223-242`).
- L push: toggles the select and edit pages (`:408-429`).
- UP and DOWN push: next or previous progression, editor-local `edit_this_progression_` with wrap 0..3 (`:390-405`). This does **not** change the `PROGRESSION` setting.
- R push: `Close()`.
- Long presses: nothing inside the editor (`:263-278`), but DOWN long still clears the CV mapping (§4).
- Effect: chords are read live by `Update` on each trigger (`APP_CHORDS.ino:737-745`), so no follow-up is needed.
- So there are 4 independent progressions, each with up to 8 chords of 5 fields, in a global array outside any settings object.

### 7. Title bar (`CHORDS_menu` `:1107-1126`)
`menu::TitleBar<0, 4, 0>`, showing:
- **the scale name of the L-encoder preview** (`OC::scale_names[left_encoder_value]`), with an indicator bitmap at x=1 when the preview equals the committed scale (`:1110-1114`);
- at x=106, "+" if `poke_octave_toggle()` else "#", followed by the **active progression** `get_active_progression() + 1` (live, driven by playmode/CV) (`:1117-1122`);
- a clock gate indicator at x=121 from `clockState()`, hidden while the chord editor is open (`:1124-1126`).

The scale editor or chord editor draws over the list (`:1161-1164`). Upstream draws them inside the row loop, which is harmless.

### 8. Screensaver
- `CHORDS_screensaver` → `Chords::RenderScreensaver(0)` (`:1360-1392`) draws the chords `0..display_num_chords` of the active progression with `menu::DrawChord`, the active chord larger. It reads only.
- `CHORDS_handleAppEvent` (`:1082-1094`): on `APP_EVENT_RESUME`, `cursor.set_editing(false); scale_editor.Close(); chord_editor.Close();`. On SUSPEND and SCREENSAVER_ON/OFF, nothing.
- In Shoal no screensaver events are sent. RESUME, after the app menu or an app switch, closes the editors.

---

## References (`APP_REFS.ino`)

### 1. Channels
- 4 `ReferenceChannel` settings objects: `ReferencesApp::channels_[DAC_CHANNEL_LAST]` (`APP_REFS.ino:673`), instance `references_app` (`:738-739`). Channel i drives DAC i (`:621-624`).
- UI: `QuadTitleBar` "A".."D" with the selected channel highlighted (`:798-803`).
- Selected with the **L encoder**, `CONSTRAIN(selected, 0, NUM_REF_CHANNELS - 0x1)` (`:990-1003`). The initial selection is `DAC_CHANNEL_FTM` (`:626`), which is **D** in this build (`:42-46`; `FLIP_180` is off).
- L-encoder "hack": `if ((cursor_pos() > 4) && (previous > selected_channel().num_enabled_settings()))` → `cursor.Init(0,0)` + `AdjustEnd`, which resets the cursor to the top. Otherwise it just calls `AdjustEnd` (`:997-1003`).

### 2. Settings table (`SETTINGS_DECLARE`, `:596-613`; enum `:85-97`)

| idx | enum | name | min | max | default | value names |
|---|---|---|---|---|---|---|
| 0 | `REF_SETTING_OCTAVE` | "Octave" | -3 | 6 | 0 | numeric (I8). The `BUCHLA_4U` and `VOR` variants (`:597-601`) are not built. |
| 1 | `REF_SETTING_SEMI` | "Semitone" | 0 | 11 | 0 | `OC::Strings::note_names_unpadded`: "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" |
| 2 | `REF_SETTING_RANGE` | "Mod range oct" | -3 | 3 | 0 | numeric. **Storage is `STORAGE_TYPE_U8` with a negative min**, so a negative value saves as 253..255 and restores clamped to **3** (`util/util_settings.h:117,137,86-92`). This is an upstream quirk. |
| 3 | `REF_SETTING_RATE` | "Mod rate (s)" | 0 | 30 | 0 | numeric |
| 4 | `REF_SETTING_NOTES_OR_BPM` | "Notes/BPM :" | 0 | 1 | 0 | `notes_or_bpm` (`:584-586`): "notes","bpm" |
| 5 | `REF_SETTING_A_ABOVE_MID_C_INTEGER` | "A above mid C" | 400 | 480 | 440 | numeric (U16) |
| 6 | `REF_SETTING_A_ABOVE_MID_C_MANTISSA` | " > mantissa" | 0 | 99 | 0 | numeric (hundredths, `:169-172`) |
| 7 | `REF_SETTING_PPQN` | "> ppqn" | 0 | 9 | 2 (`CHANNEL_PPQN_4`) | `ppqn_labels` (`:588-590`): " 1"," 2"," 4"," 8","16","24","32","48","64","96" |
| 8 | `REF_SETTING_AUTOTUNE` | "--> autotune" | 0 | 0 | 0 | none (action row) |
| 9 | `REF_SETTING_DUMMY` | "-" | 0 | 0 | 0 | none (placeholder row) |

`REF_SETTING_LAST` = 10. The array `error[]` (`:592-594`) is unused.

### 3. Visible settings
- Enabled-settings list: `num_enabled_settings` / `enabled_setting_at` / `update_enabled_settings` (`:519-548`). The list depends only on the channel, not on any value:
  - always: OCTAVE, SEMI, RANGE, RATE, AUTOTUNE;
  - then `if (DAC_CHANNEL_FTM == dac_channel_)` (channel **D**): NOTES_OR_BPM, A_ABOVE_MID_C_INTEGER, A_ABOVE_MID_C_MANTISSA, PPQN, for 9 rows;
  - `else`: DUMMY, DUMMY, for 7 rows.
- One page, no page switching.
- NOTES_OR_BPM, A_ABOVE_MID_C_*, PPQN on channels A-C are stored but never shown and never used. Only `channels_[DAC_CHANNEL_FTM]`'s copies are read (`:681, 724, 728`).
- DUMMY rows are drawn with no value; AUTOTUNE likewise (`:815-819`).
- Quirk: `REFS_restore` calls `AdjustEnd` from `channels_[0]` (7 rows) even though channel D (9 rows) is usually selected (`:764`). After a restore, the last two rows of D cannot be reached until the L encoder moves.

### 4. Controls (`REFS_handleButtonEvent` `:960-981`, `REFS_handleEncoderEvent` `:983-1016`)
Everything goes to the autotuner first while it is open (`:962-965, 985-988`). Otherwise:
- **L encoder**: selects the channel (member `ui.selected_channel`).
- **R encoder**: when editing, `change_value` on the selected channel's row (§5). Otherwise it scrolls.
- **R push**: the code tests `CONTROL_BUTTON_R == event.control` without checking the event type, but in practice only the press arrives (R long is taken by `OC_ui.cpp:168`).
  - On AUTOTUNE: `references_app.autotuner.Open(&selected_channel)`.
  - On DUMMY: nothing.
  - Otherwise: `cursor.toggle_editing()`.
- **L, UP, DOWN push** and **L/DOWN long**: nothing, outside the autotuner.
- **UP long**: consumed by Shoal and does nothing (`OC_ui.cpp:159-167`).
- **R long**: the app menu (SUSPEND, then RESUME).

Encoder-only functions:
- **Autotuner** (`OC::Autotuner<ReferenceChannel>`, `OC_autotuner.h`), opened from the "--> autotune" row. Inside, the encoders are swapped: the **R encoder moves the cursor** between DATA_SELECT and AUTOTUNE, and the **L encoder changes the value** (`OC_autotuner.h:260-268`).
  - DATA_SELECT chooses default or auto calibration for that DAC channel: `use_default()` / `use_auto_calibration()` change global `OC::DAC` calibration state (`:288-310`, `APP_REFS.ino:266-272`).
  - AUTOTUNE arms the tuner (`autotuner_arm`, `:312-322`).
  - UP push arms or disarms (`:325-337`). DOWN push runs the tuner, `autotuner_run()`, or clears an error (`:339-352`).
  - R push: `reset_autotuner()` + `Close()` (`:231-234`).
  - DOWN long: `OC::DAC::reset_all_auto_channel_calibration_data()` for all channels (`:244-246`).
- All of this changes channel member variables (`autotuner_`, `autotuner_step_`, calibration arrays, …) and global DAC calibration. **None of it is a table setting.**
- The tuner measures frequency on TR4 through `FreqMeasure` and writes `OC::DAC::update_auto_channel_calibration_data` (`:432-455`). Its status transitions run in `Autotuner::Redrawn()`, which Shoal calls from `REFS_menuRedrawn` (`:791-795`, `o_c_REV.ino:120-126`).
- **Frequency measurement**: `ReferencesApp::ISR` measures the TR4 frequency into the member `frequency_` (`:648-661`). It is display only (screensaver) and not a setting.
- There is no separate notes/octave function: octave and semitone are ordinary rows.

### 5. Guards and follow-ups (R encoder, `:1004-1012`)
- `if (setting == REF_SETTING_DUMMY) references_app.ui.cursor.set_editing(false);`. This cannot normally be reached, because R push on DUMMY does not enter edit mode. It then still calls `change_value` (max 0, a no-op).
- `selected_channel.change_value(setting, event.value); selected_channel.update_enabled_settings();`. The rebuild always runs but the list never changes, so it is not needed. There is no `AdjustEnd`.
- Nothing is applied on leaving edit mode.
- Raw `apply_value` takes effect at once: `ReferenceChannel::Update` reads OCTAVE, RANGE, RATE and SEMI every ISR tick (`:492-517`). The FTM-channel tuning and PPQN settings are read at draw time by the screensaver. Exception: while the autotuner is running (`autotuner_` true), `Update` returns early (`:494-498`), so the reference output ignores the settings.

### 6. Note masks and sub-editors
- No mask settings.
- The only sub-UI is the autotuner (§4). It writes no settings, only channel members and global DAC calibration.

### 7. Title bar
`QuadTitleBar` "A".."D" with the selected channel (`:798-803`). No live indicators in the menu. The frequency, note and BPM readout is on the screensaver only.

### 8. Screensaver
- `REFS_screensaver` (`:924-958`) draws, for each channel, the output voltage (d.ddd) and note name with an octave marker (`RenderScreensaver`, `:836-922`).
- It also shows the TR4 frequency: "TR4 %7.3f Hz", or "TR4 no input".
- Below that, either "bpm/ppqn" (`if (references_app.get_notes_or_bpm())`) or octave/note/cents relative to C0 derived from A-above-mid-C (`else if (frequency_ >= c0_freq_)`). These use channel D's settings.
- `REFS_handleAppEvent` (`:772-786`):
  - On `APP_EVENT_RESUME`: `cursor.set_editing(false); FreqMeasure.begin(); autotuner.Close();`.
  - On `SUSPEND`, `SCREENSAVER_ON` and `SCREENSAVER_OFF`: `reset_autotuner()` on all 4 channels, which clears autotune state and the per-channel `auto_calibration_data_`/target arrays and disarms. This changes state, though not settings.
- Shoal never sends the screensaver events. SUSPEND (app menu open or app switch) still resets the autotuners.

---

### Discrepancies with `SettingsAccess.inc` (Viznutcracker sweet, Acid Curds, References)
- **ByteBeat** (`Entry<ByteBeat, BYTEBEAT_SETTING_LAST, 4, &bytebeatObj>`, `bytebeatgen.bytebeats_[ch]`): correct and complete; 19 settings × 4.
  - A raw `set` of `LOOP_MODE` skips `update_enabled_settings()` + `AdjustEnd`.
  - The "default" of `TRIGGER_INPUT` is per channel (TR1..TR4 from `Init`), not the table value.
- **Chords** (`Entry<Chords, CHORDS_SETTING_LAST, 1, &chordsObj>`): the single object is right, but the mapping is **incomplete**.
  1. All chord content lives in the global `OC::g_user_chords[32]`: 4 progressions × 8 chords × {quality, inversion, voicing, base_note, octave}. It is stored with global settings and is unreachable through this table (only `CoreTest::userChordField` touches it, `CoreImpl.inc:542-547`).
  2. `menu_page_` (which list page shows) and the scale preview `left_encoder_value` are not settings.
  3. Raw `set` skips:
     - `set_scale`'s mask fix-up (SCALE);
     - `update_enabled_settings` + `AdjustEnd` (PLAYMODES, DIRECTION, SCALE);
     - `last_mask_`/`force_update_` (MASK).
  4. The table exposes `CHORD_SLOT` and `NUM_CHORDS_0..3` as 0..31, but the UI only reaches NUM_CHORDS 0..7 and never sets CHORD_SLOT. `chords_slots` has only 8 names, so value names for CHORD_SLOT > 7 index out of bounds. `PROGRESSION` uses the same 8-name array for 0..3, which is fine.
  5. `CHORD_EDIT`, `DUMMY` and `MORE_DUMMY` are action or placeholder rows with max 0.
  6. The scale editor's L-long DAC scaling page writes global `OC::DAC` scaling, outside any settings object.
- **References** (`Entry<ReferenceChannel, REF_SETTING_LAST, 4, &refsObj>`): correct as storage.
  - NOTES_OR_BPM, A_ABOVE_MID_C_INTEGER/MANTISSA and PPQN only mean anything on channel D (`DAC_CHANNEL_FTM`); the A-C copies are dead.
  - `AUTOTUNE` and `DUMMY` are non-value rows.
  - `RANGE` (−3..3 in U8 storage) loses negative values across save/restore.
  - Autotune and calibration state is outside the table.

## Cross-app facts

All paths below are relative to `src/shoal/` unless stated otherwise; firmware files are in `src/shoal/firmware/`.

### `settings::SettingsBase` (`firmware/util/util_settings.h`)

- `value_attr` (lines 37-54) holds `int default_; int min_; int max_; const char *name; const char * const *value_names; StorageType storage_type;`. In `SETTINGS_DECLARE` rows the field order is `{ default, min, max, name, value_names, storage_type }`. `value_names == nullptr` (written `NULL` in the source) means the value is shown as a number. Some apps print a value differently from what `value_names` says, for example by special-casing it in their menu draw code (the per-app sections note these cases). `storage_type` is the EEPROM width. `STORAGE_TYPE_U4` is a nibble, so values outside 0..15 are lost on save and restore even if min/max allows them. Check this for any wide range that is stored as U4.
- `clamp(v)` (49-53) clamps to `[min_, max_]`.
- `get_value(i)` (78-80) returns `values_[i]` with no bounds check and no clamp.
- `apply_value(i, v)` (86-95): `if (index < num_settings)`, clamps `v`, stores it, and returns `true` only if the stored value changed. It returns `false` if the value was unchanged or the index is out of range.
- `change_value(i, delta)` (97-99) is `apply_value(i, values_[i] + delta)`. It is a relative edit clamped to the range, with no wrap-around. Encoder acceleration only makes `delta` bigger.
- `clamp_value(i, v)` (82-84) is static.
- `value_attr(i)` (101-103) is static: the attribute table is shared by every instance of a class. So `SettingInfo` is the same for every channel, and min and max can never depend on another setting. Any dependent constraint, such as a value that must stay below the scale length, is enforced by app code, not by the table (see each app's section 5).
- `InitDefaults()` (105-108) applies the defaults. `Save`/`Restore` (110-145) pack each value at its `storage_type`, and `Restore` goes through `apply_value`, so it clamps too.
- `SETTINGS_DECLARE(clazz, last)` (235-237) defines the static `value_attr_[]` and `storage_size_` for each class.

### Menu cursor (`firmware/OC_menus.h`)

- `template <int screen_lines, bool fancy = true> class ScreenCursor` (69-146). Every app uses `menu::ScreenCursor<menu::kScreenLines>`, where `kScreenLines = 4` (line 58) rows below the title bar, each `kMenuLineH = 12` px high (line 50).
- State: `editing_`, `start_`, `end_`, `cursor_pos_` and `screen_line_` (the cursor's row on screen, 0..3).
- `Init(start, end)` (75-81) sets `cursor_pos_ = start`, `screen_line_ = 0` and `editing_ = false`. `AdjustEnd(end)` (83-86) changes only `end_`; apps call it after they rebuild their enabled-settings list.
- `Scroll(amount)` (88-114) is the only way to move the cursor. It clamps `cursor_pos_` to `[start_, end_]` and moves `screen_line_` by `amount`. In "fancy" mode it keeps one row of context, so `screen_line` stays at 1 or 2 except at the ends of the list.
- Accessors: `cursor_pos()`, `first_visible() = cursor_pos_ - screen_line_`, `last_visible()`, `editing()`, `toggle_editing()` and `set_editing(bool)`.
- There is no setter for an absolute position. To move to row `r`, call `Scroll(r - cursor_pos())`. That gives the same `screen_line_` the encoder would give for a single jump of that size, but the visible window can differ from turning one detent at a time, because the screen line comes from the step size. Friendly mode only needs `cursor_pos()` to match, so this is harmless.
- `SettingsList<screen_lines,...>` (around 430-470) walks the cursor's visible window: from `first_visible()` to `last_visible()`. It marks the item equal to `cursor_pos()` as selected, and `editing` when `cursor.editing()` is also set.
- **`cursor_pos()` is an index into the app's menu rows. In apps that keep an enabled-settings list, it is not a setting index.** Apps with an enabled list map the row with `enabled_setting_at(cursor_pos())`; see `CoreTest::menuSetting` in `CoreImpl.inc` 425-432, which does this for ASR and QQ.
- Apps keep their cursor in an app-state global. Declarations: `asr_state.cursor` and `cursor_state` (APP_ASR.ino 792-793), `h1200_state.cursor` and `cursor_state` (APP_H1200.ino 643-644), `automatonnetz_state` `grid_cursor` and `cell_cursor` (APP_AUTOMATONNETZ.ino 293-294), `qq_state.cursor` (APP_QQ.ino 1183), `dq_state.cursor` (APP_DQ.ino 1144), PolyLFO `cursor` (APP_POLYLFO.ino 241), Lorenz `cursor` (APP_LORENZ.ino 173), envgen `cursor` (APP_ENVGEN.ino 821), `seq_state.cursor` and `cursor_state` (APP_A_SEQ.ino 2058-2059), bbgen `cursor` (APP_BBGEN.ino 268), bytebeatgen `cursor` (APP_BYTEBEATGEN.ino 428), chords `cursor` (APP_CHORDS.ino 1044) and refs `cursor` (APP_REFS.ino 670).
- Several apps keep a second cursor, `cursor_state`, which they swap with `cursor` when the menu page changes (CV-mapping page and similar; see the per-app sections). Friendly mode has to know which page is current to know which row the cursor is on.
- None of these cursors is exposed through `Core.hpp`. Friendly mode needs a new per-app accessor, as `CoreTest::menuSetting` already does for two apps.

### What the shim does around UI events (`firmware/OC_ui.cpp`, `firmware/o_c_REV.ino`)

- `Ui::Poll` (OC_ui.cpp 80-115) sends `EVENT_BUTTON_PRESS` on release when the button was held for less than `kLongPressTicks`, and `EVENT_BUTTON_LONG_PRESS` otherwise. Encoder events carry the accelerated increment.
- `Ui::DispatchEvents` (117-187) sends presses and encoder turns to the app's `HandleButtonEvent` and `HandleEncoderEvent`.
  - **A long press on R** opens the global app menu (168-171); the app never sees it.
  - **A long press on UP does nothing** in Shoal (159-166, SHOAL: there is no screensaver state).
  - Every other long press, that is L push and DOWN, goes to the app as `EVENT_BUTTON_LONG_PRESS` (172-173).
- **Shoal never sends `APP_EVENT_SCREENSAVER_ON` or `APP_EVENT_SCREENSAVER_OFF`** (o_c_REV.ino 150-151 and OC_ui.cpp 184-185, both SHOAL). Only `APP_EVENT_SUSPEND` and `APP_EVENT_RESUME` are sent: from `OC_apps.ino` 401, 450 and 484, and from `Core::selectApp` (CoreImpl.inc 313/318). So each app's screensaver-event code, described per app below, is dead code in Shoal. `drawScope` calls the app's screensaver draw function directly.

### Generic settings access (`SettingsAccess.inc`, `CoreImpl.inc` 330-365)

- `kAppSettings[]` (SettingsAccess.inc 46-60) has one entry per app, in `available_apps` order (`firmware/OC_apps.ino` 54-68). The names in that table are the ones the brief uses, confirmed.
- Each entry stores a channel count (a constant) and a `T& (*Obj)(int)` accessor.

| App | Channels | Settings object for a channel |
| --- | --- | --- |
| ASR | 1 | `asr` |
| H1200 | 1 | `h1200_settings` |
| Automatonnetz | 1 | `automatonnetz_state` (grid-level only, per the comment at line 34) |
| QQ | 4 | `quantizer_channels[ch]` |
| DQ | 2 | `dq_quantizer_channels[ch]` |
| PolyLFO | 1 | `poly_lfo` |
| Lorenz | 1 | `lorenz_generator` |
| EnvGen | 4 | `envgen.envelopes_[ch]` |
| SEQ | 2 | `seq_channel[ch]` |
| BBGEN | 4 | `bbgen.balls_[ch]` |
| ByteBeat | 4 | `bytebeatgen.bytebeats_[ch]` |
| Chords | 1 | `chords` |
| REFS | 4 | `references_app.channels_[ch]` |

- `Core::channels()` and `settingCount()` return those constants for the current app (`OC::apps::index_of(global_settings.current_app_id)`).
- `settingInfo(i)` returns the static `value_attr` (`name`, `min_`, `max_`, `default_`, `value_names`); `storage_type` is dropped.
- `setting(ch, i)` is `get_value`.
- `setSettingRaw(ch, i, v)` is a bare `apply_value`, so it **bypasses every app guard and follow-up** described per app (section 5). `Entry::set` always returns `true`, even when `apply_value` returned `false`, that is, when the value did not change (SettingsAccess.inc 24). Core.hpp documents it as "raw: range-clamped, no app guards".
- Gaps that hold for every app:
  - There is no "visible settings" query (enabled list) and no cursor or edit-mode accessor.
  - There is no way to run an app's encoder-only functions.
  - Data stored outside the `SettingsBase` objects cannot be reached: the global user scales `OC::g_user_scales`, chord and sequence tables in other objects, and Automatonnetz cells.
  - Per-app gaps are listed in each app's section and summarised at the top of the file.

### Scales and note names

- Scale indices (`firmware/OC_scales.h` 18-27):
  - 0-3 are `SCALE_USER_0..3`, the four user scales.
  - `SCALE_USER_LAST = 4` is also `SCALE_NONE`, which is "Off", `braids::scales[0]`.
  - `SCALE_SEMI = 5` is "Semitone".
  - The rest are braids scales.
- `Scales::NUM_SCALES = SCALE_USER_LAST + sizeof(braids::scales)/sizeof(braids::scales[0])` (OC_scales.cpp 12). That is 4 + 98 = **102**: `braids::scales[]` in `braids_quantizer_scales.h` 37ff has 98 uncommented entries, counted by brace depth. Settings declare scale ranges as `0 .. NUM_SCALES - 1 = 101`. Chords declares `SCALE_SEMI .. 101`, so it has no user scales and no Off.
- `Scales::GetScale(i)` (OC_scales.cpp 21-26) returns `g_user_scales[i]` for `i < 4`, else `braids::scales[i - 4]`.
- `braids::Scale` (braids_quantizer.h 37-41) is `{ int16_t span; uint32_t num_notes; int16_t notes[16]; }`. **The number of meaningful bits in a scale mask is `OC::Scales::GetScale(scale).num_notes`.** It is 0 for "Off" and 12 for Semitone. For user scales it ranges from `kMinScaleLength = 4` to `kMaxScaleLength = 16` (OC_scales.h 11-12); the scale editor's length cell clamps it there (OC_scale_edit.h 375-380).
- User scales start as copies of `braids::scales[1]`, the semitone scale (OC_scales.cpp 14-18). They are a per-instance global, `OC::g_user_scales` (SHOAL_STATE, OC_scales.h 61). They are saved through `global_settings.user_scales` (OC_apps.ino 86, 158, 195, 368), not in app data. The scale editor edits a user scale in place, which **changes the scale for every app and channel that uses it** (OC_scale_edit.h 54, 343, 488, 526).
- Names: `OC::scale_names[]` (OC_scales.cpp 169ff, 102 entries) holds long names: "User-defined 1".."User-defined 4", "Off ", "Semitone", "Ionian", .... `OC::scale_names_short[]` (28ff, 102 entries) holds 4-5 character names: "USER1".."USER4", "OFF ", "SEMI", "IONI", ....
  - QQ, DQ and Chords use `scale_names` as their `value_names`. ASR and SEQ use `scale_names_short` (for example APP_ASR.ino 745, APP_A_SEQ.ino 1985).
  - Some names carry a trailing space ("Off ", "OFF ").
- Root note: `OC::Strings::note_names[12]` = `"C ", "C#", "D ", ..., "B "` (padded to 2 characters) and `note_names_unpadded[12]` (OC_strings.cpp 15-17). `OC::Strings::note_name(n)` (OC_strings.h 60-65) returns `note_names[((n % 12) + 12) % 12]`. Root settings are normally 0..11 with `OC::Strings::note_names_unpadded` (or `note_names`) as `value_names`; see each app's table.
