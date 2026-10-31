<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Fonts of the in-game windows

These fonts are for the Dear ImGui windows (`docs/imgui-plan.md`). They are not data of the game. Each font is under
the SIL Open Font License 1.1, and its license text is in this folder. The fonts are not changed.

| File | Font | Source | SHA-256 |
| --- | --- | --- | --- |
| `Roboto-Variable.ttf` | Roboto, variable (`wdth`, `wght`) | `ofl/roboto/Roboto[wdth,wght].ttf` in `google/fonts` at commit `6c1dbe27dcf7d817d4b62cec033ba77fa8e2a5ec` | `d7598e12c5dbef095ff8272cfc55da0250bd07fbdecbac8a530b9b277872a134` |
| `NotoSansJP-Regular.otf` | Noto Sans JP, Regular, the subset OTF | `Sans/SubsetOTF/JP/NotoSansJP-Regular.otf` in `notofonts/noto-cjk` at commit `f8d157532fbfaeda587e826d4cd5b21a49186f7c` | `dff723ba59d57d136764a04b9b2d03205544f7cd785a711442d6d2d085ac5073` |
| `Roboto-OFL.txt` | License of Roboto | same commit as the font | `061402327a96aadb0bfb694a960ed289ecd38d383e396243831ab81feb109c41` |
| `NotoSansJP-OFL.txt` | License of Noto Sans JP | `Sans/LICENSE`, same commit as the font | `6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2` |

Roboto is a variable font. The text renderer of Dear ImGui (stb_truetype) draws its default instance, which is the
Regular weight. Noto Sans JP is the subset OTF of the Japanese set, with the glyphs of the Japanese character sets.
It is about 4.5 MB.
