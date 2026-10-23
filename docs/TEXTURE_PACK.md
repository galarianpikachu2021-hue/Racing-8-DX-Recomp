# Texture packs (custom textures, e.g. a 4K UI)

The native renderer can dump the game's colour textures and load replacements of any size. Positions never change:
the game draws the HUD in its own 1920x1080 (docked) or 1280x720 (handheld) layout and samples every texture by UV, so a
replacement with the same picture at 2x or 4x the pixels lands in exactly the same place and just looks sharper.

## Dump

`KARTRECOMP_TEXTURE_DUMP=1` (native renderer) writes every block-linear 2D colour texture once as an RGBA8 PNG:

    local/game/user/dump/textures/<hash16>_<width>x<height>_<tag>.png      tag = rgba, bgra, bc1, bc2, bc3, bc7, astc

`<hash16>` is the CityHash64 of the texture's guest bytes (16 hex digits). One short race dumps about 450 textures
(characters, items, HUD icons, menu art). Pitch-linear textures (the text glyph cache, video frames), cube maps, arrays,
3D textures and one/two-channel formats (BC4/BC5/R8) are not dumped or replaced.

## Replace

Put a PNG into `local/game/user/load/textures/` (or the folder in `KARTRECOMP_TEXTURE_PACK`; sub-folders are fine).
Any file name containing a 16-hex-digit hash replaces that texture, at any size, e.g.
`5768e95c697fa822_256x256_rgba.png` (a 4x version of the 64x64 minimap icon). Use the same colour encoding as the dump
(the dump is the game's colours as stored: sRGB textures stay sRGB). Replacements get a box-filtered mip chain. The pack
is read at start-up; the log says "texture pack: N replacement(s)".

Verified: the minimap's Link icon replaced by a recoloured 4x PNG showed up at the same size and place.

## Notes for pack makers

- Keep the picture's aspect ratio and alignment (a transparent margin, if the original has one, must scale too).
- Shaders that read textures by pixel coordinates (texelFetch) would see the new size; UI shaders sample by UV.
- `tools/make-overlay-font.py` and the overlay menu are separate: the in-game menu is drawn by the renderer itself.
