Placeholder contrast deltas

- PLACEHOLDER_PRIMARY_DELTA_DARK / PLACEHOLDER_PRIMARY_DELTA_LIGHT  
    Amount added to (dark) or subtracted from (light) the base lightness for the primary diagonal lines. Larger absolute value = stronger contrast = stronger adaptation (reduces perceived title tint variance) but risks the placeholder drawing too much attention. If you still see variance, this is the first knob to increase (e.g. 105 → 115). Avoid going so high that the value clips (near pure white or black), which flattens detail.
- PLACEHOLDER_SECONDARY_DELTA_DARK / PLACEHOLDER_SECONDARY_DELTA_LIGHT  
    Same idea for the secondary (sparser) diagonal lines. Usually a smaller magnitude than the primary to create a secondary layer without overpowering it. Raise slightly (e.g. 55 → 65) only if you want more multi‑level contrast.

Line opacity

- PLACEHOLDER_PRIMARY_ALPHA / PLACEHOLDER_SECONDARY_ALPHA  
    Transparency (0–255) of the two line sets after color and lightness adjustments. Increasing alpha strengthens the pattern without changing hue/lightness math. If bumping contrast deltas starts to look harsh, you can instead keep deltas and raise alpha modestly (e.g. 215 → 225). Lower alpha if the pattern competes visually with real artwork.

Pattern density

- PLACEHOLDER_STEP_MIN / PLACEHOLDER_STEP_MAX  
    The code derives step ~ (min(width,height)/10) clamped to these bounds. Decreasing STEP_MIN or increasing STEP_MAX allows denser lines on small or large thumbnails respectively. Denser = more adaptation but greater risk of moiré / visual noise. Keep within roughly 4–14 for most DPI scenarios.

Micro noise

- PLACEHOLDER_NOISE_AMPLITUDE  
    Maximum absolute RGB perturbation applied to sampled pixels (±value). Higher amplitude increases fine-grain variation; too high causes sparkle or graininess around edges. Use 0–5 (3 is subtle; 4–5 if you still see variance).
- PLACEHOLDER_NOISE_STRIDE  
    Pixel spacing for applying noise (both axes). Larger stride = fewer modified pixels (lower cost, softer effect). Smaller stride (e.g. 2) distributes noise more evenly but can look speckled. Typical useful range: 2–6. Setting amplitude > 0 with a very small stride amplifies visual noise quickly—balance them.

Gradient shaping

- PLACEHOLDER_GRADIENT_TOP_ALPHA / PLACEHOLDER_GRADIENT_BOTTOM_ALPHA  
    Alpha of a vertical overlay gradient using the base color. Purpose: introduce large-scale luminance modulation without erasing lines. Increasing these values smooths the pattern slightly (lines integrate more into the base) and can reduce any residual hotspot effect. If lines feel washed out, reduce top alpha first.

Title tint blending

- PLACEHOLDER_PRIMARY_TINT_NUM / PLACEHOLDER_PRIMARY_TINT_DEN  
    Weighting of base-adjusted primary line color vs titleTint: effective mix = (primary * NUM + titleTint * (DEN - NUM)) / DEN. Larger NUM → less color pulled toward titleTint. Lower NUM → more hue coherence with the title, which can slightly reduce perceived variance but risks the placeholder picking up too much accent. Typical: keep DEN stable (5 or 6) and adjust NUM by ±1 if you want more/less tint influence.
- PLACEHOLDER_SECONDARY_TINT_NUM / PLACEHOLDER_SECONDARY_TINT_DEN  
    Same formula for secondary lines. Because secondary lines are already lower contrast, giving them a bit more tint (slightly smaller NUM relative to DEN) can unify the field without over-accenting the entire placeholder.

Tuning strategy (practical order)

1. Primary contrast: Adjust PRIMARY_DELTA_* first (small increments of 5–10).
2. If still slight variance: Increase PRIMARY_ALPHA a little (≤ +10).
3. Add more global structure: Slightly raise SECONDARY_DELTA_* (≤ +10) or reduce STEP_MIN (one unit).
4. Subtle fine-grain: Increase NOISE_AMPLITUDE by 1 (only if needed).
5. Color cohesion: If placeholder hue feels detached, decrement PRIMARY_TINT_NUM (e.g. 3 → 2) to lean lines a bit toward titleTint; if it becomes too accent-colored, revert.

Avoid overdoing all knobs simultaneously—each adds adaptation; stacking them all at maximum will make empty placeholders visually louder than real artwork.

Performance considerations

- Noise cost scales roughly with (w/stride) * (h/stride). Larger stride greatly reduces work.
- Caching already prevents recomputation per identical (size, base color) key, so you can afford slightly denser patterns unless sizes vary heavily.

Dark vs light theme edge cases If your palette’s Mid for light mode is already fairly high (lightness > 200), very large negative deltas can push primary lines near black, which may look harsher than intended. In that case reduce absolute magnitude (e.g. -105 → -90 for light). Similarly for extremely dark bases (lightness < 40) you may not need the full +105; +95 can suffice.

Quick diagnostic adjustments

- Want “just a bit more adaptation”: primaryDelta +10 OR primaryAlpha +10.
- Pattern too busy: increase STEP_MIN by 1 or reduce secondaryAlpha by ~15.
- Still slight tint shift: increase secondaryDelta by +5 and noiseAmplitude +1 (unless already ≥5).

Once you lock values that look good across both themes and multiple monitor brightness levels, they should remain stable; only revisit if you change the palette basis (e.g. switch Mid source).