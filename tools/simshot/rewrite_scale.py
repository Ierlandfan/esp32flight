#!/usr/bin/env python3
"""Mechanical downscale pass: wrap design-space integer literals in LVGL
geometry calls with UISX/UISY (identity on >=800x480), and map fonts to
their small tiers via UIFONT. Idempotent-ish: skips already-wrapped args."""
import re
import sys

# call -> per-argument axis, 1-based arg index after the object argument(s).
# 'x'/'y' = wrap literals in that arg with that axis, None = leave alone.
CALLS = {
    "lv_obj_set_size":            {2: "x", 3: "y"},
    "lv_obj_set_pos":             {2: "x", 3: "y"},
    "lv_obj_set_width":           {2: "x"},
    "lv_obj_set_height":          {2: "y"},
    "lv_obj_set_x":               {2: "x"},
    "lv_obj_set_y":               {2: "y"},
    "lv_obj_align":               {3: "x", 4: "y"},
    "lv_obj_align_to":            {4: "x", 5: "y"},
    "lv_obj_set_style_pad_left":  {2: "x"},
    "lv_obj_set_style_pad_right": {2: "x"},
    "lv_obj_set_style_pad_hor":   {2: "x"},
    "lv_obj_set_style_pad_column":{2: "x"},
    "lv_obj_set_style_pad_top":   {2: "y"},
    "lv_obj_set_style_pad_bottom":{2: "y"},
    "lv_obj_set_style_pad_ver":   {2: "y"},
    "lv_obj_set_style_pad_row":   {2: "y"},
    "lv_obj_set_style_pad_all":   {2: "y"},
    "lv_obj_set_style_pad_gap":   {2: "y"},
    "lv_obj_set_style_radius":    {2: "y"},
    "lv_obj_set_style_text_line_space": {2: "y"},
    "lv_obj_set_style_translate_x": {2: "x"},
    "lv_obj_set_style_translate_y": {2: "y"},
}

FONT_MAP = {
    "font_pl_14": "font_pl_8",
    "font_pl_16": "font_pl_10",
    "font_pl_20": "font_pl_12",
    "lv_font_montserrat_12": "lv_font_montserrat_8",
    "lv_font_montserrat_14": "lv_font_montserrat_8",
    "lv_font_montserrat_16": "lv_font_montserrat_10",
    "lv_font_montserrat_20": "lv_font_montserrat_12",
    "lv_font_montserrat_24": "lv_font_montserrat_14",
    "lv_font_montserrat_28": "lv_font_montserrat_16",
    "lv_font_montserrat_32": "lv_font_montserrat_20",
    "lv_font_montserrat_44": "lv_font_montserrat_28",
}

SKIP_TOKENS = ("LV_PCT", "lv_pct", "LV_SIZE_CONTENT", "UISX", "UISY")


def split_args(s):
    args, depth, cur = [], 0, ""
    for ch in s:
        if ch == "," and depth == 0:
            args.append(cur)
            cur = ""
            continue
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        cur += ch
    args.append(cur)
    return args


def wrap_literals(arg, axis):
    if any(t in arg for t in SKIP_TOKENS):
        return arg
    macro = "UISX" if axis == "x" else "UISY"

    def repl(m):
        v = int(m.group(0))
        if v <= 2:   # hairlines and zero offsets keep their pixel meaning
            return m.group(0)
        return f"{macro}({v})"

    return re.sub(r"(?<![\w.])\d+(?![\w.])", repl, arg)


def find_calls(src, name):
    out = []
    for m in re.finditer(rf"\b{name}\s*\(", src):
        depth, i = 1, m.end()
        while depth > 0 and i < len(src):
            if src[i] == "(":
                depth += 1
            elif src[i] == ")":
                depth -= 1
            i += 1
        out.append((m.end(), i - 1))
    return out


def process(path):
    src = open(path).read()
    changed = 0

    for name, axes in CALLS.items():
        while True:
            done = True
            for a, b in find_calls(src, name):
                inner = src[a:b]
                args = split_args(inner)
                new_args = []
                for idx, arg in enumerate(args, start=1):
                    ax = axes.get(idx)
                    new_args.append(wrap_literals(arg, ax) if ax else arg)
                new_inner = ",".join(new_args)
                if new_inner != inner:
                    src = src[:a] + new_inner + src[b:]
                    changed += 1
                    done = False
                    break   # offsets shifted; rescan this call name
            if done:
                break

    def font_repl(m):
        big = m.group(1)
        small = FONT_MAP[big]
        return f"UIFONT(&{big}, &{small})"

    pat = r"&(" + "|".join(FONT_MAP) + r")\b"
    src, nfonts = re.subn(pat, font_repl, src)
    changed += nfonts

    open(path, "w").write(src)
    print(f"{path}: {changed} rewrites")


for p in sys.argv[1:]:
    process(p)
