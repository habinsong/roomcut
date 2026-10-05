#!/usr/bin/env python3
"""Turn tune.cpp RESULT lines into the SpacePresetLibrary tables.

    python3 scripts/space-presets/gen-swift.py <results dir>   # prints the two arrays

Names, groups and the order in the list live here; every value comes from the
RESULT lines (tune's stdout, one file per preset).
"""
import glob, os, re, sys
meta = {
 # id: (group, en, ja, fr, de)
 "sp-reference": ("everyday", "Reference", "リファレンス", "Référence", "Referenz"),
 "sp-voice": ("everyday", "Voice", "ボイス", "Voix", "Stimme"),
 "sp-podcast": ("everyday", "Podcast Booth", "ポッドキャストブース", "Cabine podcast", "Podcast-Kabine"),
 "sp-meeting": ("everyday", "Meeting Room", "会議室", "Salle de réunion", "Besprechungsraum"),
 "sp-living": ("everyday", "Living Room", "リビングルーム", "Salon", "Wohnzimmer"),
 "sp-studio": ("music", "Studio", "スタジオ", "Studio", "Studio"),
 "sp-lounge": ("music", "Lounge", "ラウンジ", "Lounge", "Lounge"),
 "sp-ambience": ("music", "Ambience", "アンビエンス", "Ambiance", "Ambiente"),
 "sp-chamber": ("music", "Chamber Hall", "室内楽ホール", "Salle de chambre", "Kammersaal"),
 "sp-concert": ("music", "Concert Hall", "コンサートホール", "Salle de concert", "Konzertsaal"),
 "sp-gaming": ("cinema", "Gaming", "ゲーム", "Jeu", "Gaming"),
 "sp-hometheater": ("cinema", "Home Theater", "ホームシアター", "Home cinéma", "Heimkino"),
 "sp-widestage": ("cinema", "Wide Stage", "ワイドステージ", "Scène large", "Breite Bühne"),
 "sp-cinema": ("cinema", "Cinema", "シネマ", "Cinéma", "Kino"),
 "sp-tvshow": ("cinema", "TV Show", "テレビ番組", "Émission TV", "TV-Show"),
 "hp-reference": ("everyday", "Reference", "リファレンス", "Référence", "Referenz"),
 "hp-voice": ("everyday", "Voice", "ボイス", "Voix", "Stimme"),
 "hp-podcast": ("everyday", "Podcast Booth", "ポッドキャストブース", "Cabine podcast", "Podcast-Kabine"),
 "hp-living": ("everyday", "Living Room", "リビングルーム", "Salon", "Wohnzimmer"),
 "hp-ambience": ("everyday", "Ambience", "アンビエンス", "Ambiance", "Ambiente"),
 "hp-studio": ("music", "Studio", "スタジオ", "Studio", "Studio"),
 "hp-lounge": ("music", "Lounge", "ラウンジ", "Lounge", "Lounge"),
 "hp-concert": ("music", "Concert Hall", "コンサートホール", "Salle de concert", "Konzertsaal"),
 "hp-surroundmusic": ("music", "Surround Music", "サラウンドミュージック", "Musique surround", "Surround-Musik"),
 "hp-orchestra": ("music", "Orchestra", "オーケストラ", "Orchestre", "Orchester"),
 "hp-hometheater": ("cinema", "Home Theater", "ホームシアター", "Home cinéma", "Heimkino"),
 "hp-cinema71": ("cinema", "Cinema 7.1", "シネマ 7.1", "Cinéma 7.1", "Kino 7.1"),
 "hp-living71": ("cinema", "Living Room 7.1", "リビングルーム 7.1", "Salon 7.1", "Wohnzimmer 7.1"),
 "hp-gaming71": ("cinema", "Gaming 7.1", "ゲーム 7.1", "Jeu 7.1", "Gaming 7.1"),
 "hp-dialogue51": ("cinema", "Dialogue 5.1", "ダイアログ 5.1", "Dialogues 5.1", "Dialog 5.1"),
}
genres = [("pop", "Pop", "ポップ", "Pop", "Pop"), ("ballad", "Ballad", "バラード", "Ballade", "Ballade"),
          ("rock", "Rock", "ロック", "Rock", "Rock"), ("hiphop", "Hip-Hop", "ヒップホップ", "Hip-hop", "Hip-Hop"),
          ("electronic", "Electronic", "エレクトロニック", "Électronique", "Elektronisch"), ("jazz", "Jazz", "ジャズ", "Jazz", "Jazz"),
          ("classical", "Classical", "クラシック", "Classique", "Klassik"), ("acoustic", "Acoustic", "アコースティック", "Acoustique", "Akustisch")]
for out in ("sp", "hp"):
    for g, en, ja, fr, de in genres:
        meta[f"{out}-{g}"] = ("music", en, ja, fr, de)
order_sp = ["sp-reference","sp-voice","sp-podcast","sp-meeting","sp-living"] + [f"sp-{g[0]}" for g in genres] + ["sp-studio","sp-lounge","sp-ambience","sp-chamber","sp-concert","sp-gaming","sp-hometheater","sp-widestage","sp-cinema","sp-tvshow"]
order_hp = ["hp-reference","hp-voice","hp-podcast","hp-living","hp-ambience"] + [f"hp-{g[0]}" for g in genres] + ["hp-studio","hp-lounge","hp-concert","hp-surroundmusic","hp-orchestra","hp-hometheater","hp-cinema71","hp-living71","hp-gaming71","hp-dialogue51"]
res = {"sp-reference": dict(surround=0, room=0, amount=50, cw=100, depth=50, width=0, center=0, damping=0, cross=0),
       "hp-reference": dict(surround=0, room=0, amount=50, cw=100, depth=50, width=0, center=0, damping=0, cross=0)}
for f in glob.glob(os.path.join(sys.argv[1] if len(sys.argv) > 1 else "results", "*.txt")):
    for line in open(f):
        if line.startswith("RESULT"):
            parts = line.split()
            d = {k: float(v) for k, v in (x.split("=") for x in parts[2:11])}
            res[parts[1]] = d
sur = {0: None, 1: ".ambience", 2: ".virtual51", 3: ".virtual71"}
def num(v): return str(int(v)) if v == int(v) else str(v)
def line(pid):
    grp, en, ja, fr, de = meta[pid]
    d = res[pid]; hp = pid.startswith("hp-")
    args = [f'"{pid}"', "true" if hp else "false", "." + grp, f'("{en}", "{ja}", "{fr}", "{de}")']
    kw = []
    if sur[int(d["surround"])]: kw.append(f"surround: {sur[int(d['surround'])]}")
    if d["room"]: kw += [f"room: {num(d['room'])}", f"amount: {num(d['amount'])}"]
    for k, name in (("width","width"),("center","center"),("damping","damping"),("cross","cross")):
        if d[k]: kw.append(f"{name}: {num(d[k])}")
    if hp and d["surround"] >= 2:
        if d["cw"] != 100: kw.append(f"centerWidth: {num(d['cw'])}")
        if d["depth"] != 50: kw.append(f"depth: {num(d['depth'])}")
    return "        make(" + ", ".join(args + kw) + "),"
missing = [p for p in order_sp + order_hp if p not in res]
if missing: print("MISSING", missing, file=sys.stderr)
print("    static let speakers: [SpacePreset] = [")
for p in order_sp:
    if p in res: print(line(p))
print("    ]\n\n    static let headphones: [SpacePreset] = [")
for p in order_hp:
    if p in res: print(line(p))
print("    ]")
