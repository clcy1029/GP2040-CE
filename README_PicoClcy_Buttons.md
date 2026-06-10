# PicoClcy — Custom Button Reference (一键出招总表)

All the custom one-button moves Chang built into the **Input Reverse** add-on (`src/addons/reverse.cpp`).
Assign any of them to a GPIO in the web **Pin Mapping** dropdown (Add-ons → Input Reverse must stay
**Enabled** — every move below runs inside that add-on).

**Attack ↔ button on this board:** `LP=B1 · MP=B2 · HP=R1 · LK=B3 · MK=B4 · HK=L1`

**Numpad notation:** `1=↓← 2=↓ 3=↓→ 4=← 5=neutral 6=→ 7=↑← 8=↑ 9=↑→`

There are **3 move engines + Drive Reversal + macros**:

| Engine | Mirrors your direction? | Gated? | Ender (attack) |
|---|---|---|---|
| ① Super | shared opening, then a tail picked from your input | — | your pressed attack, or button default |
| ② Fixed motion | no (absolute sequence) | no | per-move fixed, *or* your pressed attack |
| ③ Directional | yes (hold back → output forward) | some | per-move fixed (+ held attack on 46s) |
| ④ Drive Reversal | reverses ←↔→ | yes (needs ←/→) | presses L2 |

> **Mirror rule (③):** hold **←/↙/↖ → "forward" = 6 (→)**; hold **→/↘/↗ → "forward" = 4 (←)**.
> So the same button works on both sides — you just hold "back".

---

## ① Super — `Reverse 23626 LP` / `Reverse 23626 LK`

Press → immediately play the shared opening **`21346`** (2,1,3,4,6, 1 frame each). From the press it
watches for **any attack** and a **held ←/→**. When the opening's final `6` finishes it picks a tail
(reusing the 21346 already played — no restart):

| During the opening you… | Result | Ender | 
|---|---|---|
| pressed **any attack** (k or p) | `21346246` (+ 2,4,6) | **the exact attack(s) you pressed** |
| held **← (4)**, no attack | `2134626` (+ 2,6) | button default (**LP** / **LK**) |
| held **→ (6)**, no attack | `2134624` (+ 2,4) | button default (**LP** / **LK**) |
| nothing | stops after `21346` | — |

- `Reverse 23626 LP` default ender = **LP**; `Reverse 23626 LK` default = **LK**.
- Opening steps 1 frame; the **final attack step is 2-3 frames (random)**.
- Held attacks are suppressed during the motion so they don't leak.

---

## ② Fixed-motion specials

Play a fixed numpad sequence (no mirror, no gate), to completion. Two ender styles:
- **Your attack = ender** — `236 / 214 / 6214 / 4236 / 22`: the attack you're holding **at the last step**
  comes out (sampled *only* then — a leftover/early press that's released by the last step is ignored).
- **Fixed ender** — everything else fires its built-in attack regardless of what you press.

| Dropdown name | Enum | Sequence | Ender |
|---|---|---|---|
| 5236 QCR | `BUTTON_PRESS_QCR_236` | 5,2,3,6 | your pressed attack |
| 5214 QCR | `BUTTON_PRESS_QCR_214` | 5,2,1,4 | your pressed attack |
| 6214 HCB | `BUTTON_PRESS_6214_HCB` | 6,2,1,4 | your pressed attack |
| 6214 LP | `BUTTON_PRESS_6214_LP` | 6,2,1,4 | LP **+ a held punch** (add); a **held kick** replaces it (kick only) |
| 4236 HCB | `BUTTON_PRESS_4236_HCB` | 4,2,3,6 | your pressed attack |
| 4236 LP | `BUTTON_PRESS_4236_LP` | 4,2,3,6 | LP **+ a held punch** (add); a **held kick** replaces it (kick only) |
| 623 LP | `BUTTON_PRESS_623_LP` | 623 (DP) | LP |
| 623 MP | `BUTTON_PRESS_623_MP` | 623 (DP) | MP |
| 623 HP | `BUTTON_PRESS_623_HP` | 623 (DP) | HP |
| 623 LPMP | `BUTTON_PRESS_623_LPMP` | 623 (DP) | LP+MP |
| 623 LK | `BUTTON_PRESS_623_LK` | 623 (DP) | LK |
| 623 MK | `BUTTON_PRESS_623_MK` | 623 (DP) | MK |
| 623 HK | `BUTTON_PRESS_623_HK` | 623 (DP) | HK |
| 623 LKMK | `BUTTON_PRESS_623_LKMK` | 623 (DP) | LK+MK |
| 623 HP Charge | `BUTTON_PRESS_623_HP_CHARGE` | 1,3,1,3,1 | HP — **held as long as you keep the button pressed** (tap = one-shot) |
| 21346 LP | `BUTTON_PRESS_21346_LP` | 2,1,3,4,6 | LP **+ a held punch** (MP/HP, add); but a **held kick** (LK/MK/HK) **replaces** LP (kick only) — read at the last step |
| 21346 LK | `BUTTON_PRESS_21346_LK` | 2,1,3,4,6 | LK *(a kick held at the last step overrides)* |
| 21346 HK | `BUTTON_PRESS_21346_HK` | 2,1,3,4,6 | HK *(a kick held at the last step overrides)* |
| 21346 LKMK | `BUTTON_PRESS_21346_LKMK` | 2,1,3,4,6 | LK+MK |
| 21346246 LK | `BUTTON_PRESS_21346246_LK` | 2,1,3,4,6,2,4,6 | LK |
| 21346246 LP | `BUTTON_PRESS_21346246_LP` | 2,1,3,4,6,2,4,6 | LP |
| 214236 LK | `BUTTON_PRESS_214236_LK` | 2,1,4,2,3,6 | LK |
| 214236 MK | `BUTTON_PRESS_214236_MK` | 2,1,4,2,3,6 | MK |
| 214236 HK | `BUTTON_PRESS_214236_HK` | 2,1,4,2,3,6 | HK |
| 214236 LKMK | `BUTTON_PRESS_214236_LKMK` | 2,1,4,2,3,6 | LK+MK |
| 22 LKMK | `BUTTON_PRESS_22_LKMK` | 2,2 | LK+MK |
| 22 LK | `BUTTON_PRESS_22_LK` | 2,2 | LK |
| 22 LP | `BUTTON_PRESS_22_LP` | 2,2 | LP — **but an attack held at the last step replaces it** |
| 22 | `BUTTON_PRESS_22` | 2,2 | your pressed attack |
| Anti Air 2HP | `BUTTON_PRESS_2_HP` | 2 (crouch, 4f) | HP |
| 2PP | `BUTTON_PRESS_2PP` | 2 (crouch, 2f) | LP+MP |
| KKK | `BUTTON_PRESS_KKK` | (neutral, 2f) | LK+MK+HK — ignores all input (not the directional Reversal KKK) |

> `623` is played as a `1,3,1,3,1` shortcut. Per-step length is a random 1-2 frames (last attack step
> 2-3) with overrides: `623 HP` steps = 1f + last 1-3f, `Anti Air 2HP` = 4f, `21346246` = all 1f, `214236` = 1f steps + last 2f,
> `5236`/`5214 QCR` = leading 回中 neutral 2f then each dir 2-3f, `6214`/`4236 HCB` = each step 2-3f.
> **`623 HP Charge`** is special — it plays `1,3,1,3,1` (each 1-2f) then **keeps HP pressed until you
> release** the button (the only move that holds after its sequence; while held the stick passes through).

---

## ③ Directional moves (sample direction at press + mirror)

Read the held direction the instant you press, mirror it, and (for gated moves) bail out if the
required direction isn't held. See the mirror rule at the top.

| Dropdown name | Enum | Gate | Output | Timing |
|---|---|---|---|---|
| 46 LP | `BUTTON_PRESS_46_LP` | needs ←/→ | forward + **LP** + any held **punch** | 2-3f |
| 46 MP | `BUTTON_PRESS_46_MP` | needs ←/→ | forward + **MP** + any held **punch** | 2-3f |
| 46 HK | `BUTTON_PRESS_46_HK` | needs ←/→ | forward + **HK** + any held **kick** | 2-3f |
| 46 MK | `BUTTON_PRESS_46_MK` | needs ←/→ | forward + **MK** + any held **kick** | 2-3f |
| 1 or 3 HP | `BUTTON_PRESS_13_HP` | needs ←/→ | ↓-forward (**3** / **1**) + **HP** | 3-4f |
| 1 or 3 HK | `BUTTON_PRESS_13_HK` | needs ←/→ | ↓-forward (**3** / **1**) + **HK** | 3-4f |
| 28 HK | `BUTTON_PRESS_28_HK` | needs **↓** held | **8 (↑) + HK** | 2-3f |
| 28 LK | `BUTTON_PRESS_28_LK` | needs **↓** held | **8 (↑) + LK** | 2-3f |
| 28 LKMK | `BUTTON_PRESS_28_LKMK` | needs **↓** held | **8 (↑) + LK+MK** | 2-3f |
| Air Throw | `BUTTON_PRESS_AIR_THROW` | none | ① jump ↑+forward ② **LK+LP** | 2-3f/step |
| JMP | `BUTTON_PRESS_JMP` | none | ① jump ↑+forward ② **MP** | 2-3f/step |
| Reversal KKK | `BUTTON_PRESS_REVERSAL_KKK` | needs ←/→ | forward + **LK+MK+HK** (all 3 kicks) | 2-3f |
| Anti Air 4MK | `BUTTON_PRESS_ANTI_AIR_4MK` | needs ←/→ | **held ←/→ as-is** (no mirror, ↓ stripped) + **MK** | 2-3f |

Notes:
- **46 LP/MP/HK/MK** also OR-in any *same-category* attack — held at press **or pressed during the move**
  (added the instant you hit it, so e.g. hold ←, press 46 LP + HP → first frame is already `6 + LP+HP`):
  46 LP/MP→punches, 46 HK/MK→kicks. The `1 or 3`, `28`, `Reversal KKK`, and `Anti Air 4MK` moves use a fixed ender (no add).
- **Anti Air 4MK** does NOT mirror — it outputs the side you're *actually* holding (←→4, →→6; 1→4, 3→6 with ↓ dropped) + MK.
- **28 ×** are charge moves: you must already be **holding ↓** (1/2/3 all count). If no ↓ is held,
  nothing comes out — the firmware just fires `8+attack` to cash in your charge.
- **Air Throw / JMP** don't gate: with no ←/→ they jump straight up (`↑`) instead of toward the opponent.

---

## ④ Drive Reversal — `Drive Reversal`

| Enum | Behavior |
|---|---|
| `BUTTON_PRESS_INPUT_REVERSE` | **Gated**: only acts while a **←/→** is held → reverses the d-pad (←↔→) and presses **L2**. No left/right held → does nothing. |

---

## ⑤ Macros

`Macro Button` + `Macro 1` … `Macro 6` — configured in the web UI (Add-ons → Input Macro). Off unless
a macro pin is assigned. While a macro runs, the d-pad is ignored and attacks you press are deferred
and fired together on the macro's last frame.

---

## Behavior shared by all moves

- The three move engines (**super / fixed-motion / directional**) are **mutually exclusive** — only one
  runs at a time; pressing another move's button mid-motion is ignored until the current one finishes.
- While any move plays, your **held attacks are suppressed** so they don't leak out as a stray normal.
- Per-step durations are **randomized** (in the frame ranges above) so the input pattern varies slightly
  each time — looks natural in replays.
- Everything lives in **`src/addons/reverse.cpp`** (tables `MOTION_DEFS`, `DIR_MOVE_DEFS`, and the super
  block); enums in `proto/enums.proto`; dropdown labels + order in `www/src/…/PinMapping`.
