# 3DS tools

Work towards a 3DS app that builds a `Widescreen.cxi` for TWiLight Menu++ with
circle pad and gyro built in (plan: own TwlBg patcher, written fresh).

## twlbg.py

Takes apart a TwlBg module on a PC: its code, unpacked, for study and to check
our packer. The dump comes from your own console and goes in `dumps/`, which
git ignores. Never commit or share it: it is Nintendo's code.

Getting it, in GodMode9: SYSNAND CTRNAND → `title/00040138/` →
`00000102` (Old 3DS) or `20000102` (New 3DS) → `content/` → the lowest-numbered
`.app` → "NCCH image options" → "Mount image to drive", then copy
`exefs/.firm` (that is TWL_FIRM, with TwlBg inside) to the SD card, e.g. as
`dumps/o3ds_twl.firm`. The mounted image's `exefs.bin` works too.

    tools/3ds/twlbg.py info   tools/3ds/dumps/o3ds_twl.firm
    tools/3ds/twlbg.py check  tools/3ds/dumps/o3ds_twl.firm
    tools/3ds/twlbg.py unpack tools/3ds/dumps/o3ds_twl.firm tools/3ds/dumps/o3ds
    tools/3ds/twlbg.py disasm tools/3ds/dumps/o3ds_twl.firm tools/3ds/dumps/o3ds.txt
    tools/3ds/twlbg.py selftest

`check` unpacks the code, packs it with our packer, unpacks that and compares,
and says whether ours fits where the original was.
