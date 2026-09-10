<div align="center">

<img src="../icon/roomcut_icon.png" alt="Roomcut" width="184" height="184">

# Roomcut

[English](../README.md) · [한국어](README.ko.md) · **日本語** · [Français](README.fr.md) · [Deutsch](README.de.md)

[![Download](https://img.shields.io/github/v/release/habinsong/roomcut?style=for-the-badge&label=download&color=2EA043)](https://github.com/habinsong/roomcut/releases/latest) [![License](https://img.shields.io/badge/license-Apache--2.0-D22128?style=for-the-badge)](../LICENSE) ![macOS 26+](https://img.shields.io/badge/macOS-26%2B-000000?style=for-the-badge) ![Apple Silicon](https://img.shields.io/badge/Apple-Silicon-555555?style=for-the-badge) ![Local-first](https://img.shields.io/badge/LOCAL--FIRST-1f2328?style=for-the-badge)

</div>

> **公式リポジトリ**
>
> Roomcut は [habinsong](https://github.com/habinsong) が制作・管理しています。このリポジトリで明記していないコピー、ミラー、リブランド、類似プロジェクトは Roomcut と関係ありません。ソースコードは Apache License 2.0 で提供します。Roomcut の名前、ブランド、スクリーンショット、文書は © 2026 송하빈 であり、このライセンスには含まれません。

macOS にはシステム全体の EQ がありません。スピーカーが 120Hz で 6dB 出すぎていても、EQ を
積んだアプリの中でしか直せません。

Roomcut は **Roomcut Output** という出力デバイスを 1 つ足します。macOS の出力をそこへ向ければ、
Spotify も Safari も Zoom も通知音も、DSP チェーンを通ってから実際に聴くデバイスへ出ていきます。
この仮想デバイスはリポジトリの中にある CoreAudio Audio Server Plug-in です。BlackHole や
Soundflower を下に敷く必要はありません。

<div align="center">
<table>
<tr>
<td align="center" valign="middle"><img src="../icon/app/main_home.png" alt="Roomcut ホーム" width="200"><br><sub>ホーム</sub></td>
<td align="center" valign="middle"><img src="../icon/app/menubar.png" alt="Roomcut メニューバー" width="270"><br><sub>メニューバー</sub></td>
<td align="center" valign="middle"><img src="../icon/app/compact_mode.png" alt="Roomcut コンパクトモード" width="220"><br><sub>コンパクトモード</sub></td>
</tr>
</table>
</div>

## 中身

**EQ。** 31Hz から 16kHz までのグラフィック 10 バンドと、その上にパラメトリック 6 バンド
(ベル、ロー/ハイシェルフ、ハイパス、ローパス、ノッチ)。周波数で考えたくないときのために、
Bass・Warmth・Vocal・Clarity・Air のマクロ 5 つが関係するバンドをまとめて動かします。
ベルとシェルフの帯域はダイナミックにできます。しきい値とレンジを与えると、その帯域が実際に
大きくなったときだけ下がります。ふだんは問題なく、ある音だけで暴れる共振に使えます。
チェーンの最後はルックアヘッド 2ms のリミッターです。

**ステレオの広がり。** 最近のマスターは横に広く作られていて、ノートブックのスピーカーだと
ボーカルが真ん中から外れることがあります。Focus を上げるとサイドが内側に寄り、ボーカルが
真ん中に戻ります。Space はその逆です。どちらもサイド信号にしか効かないので、真ん中にある
ボーカルはスライダーを振り切っても手つかずのまま出ます。この条件のために一度作り直しました。
最初はミッドの位相を回したコピーを足して広げていて、モノ互換ではあるのに定位が崩れ、
スライダーを上げるほどボーカルが左へ流れていきました。Space は ±200、Center と Damping は
200 まで動きます。カーブは変えていないので、いま使っている値はそのままの音です。同じタブに
Crossfeed とスピーカー/ヘッドフォンの切り替えがあります。

**音量を合わせた A/B。** スロットは 2 つ、それぞれが自分の編集履歴を持ちます。`⌘Z` と `⇧⌘Z`
は今いる側に効き、コピーボタンは今の設定をもう一方へ渡します。レベルを入れると、リミッターまで
含めた両方のチェーンを同じ区間で K 特性のメーターにかけ、大きいほうを下げます。音量ではなく音を
比べるための機能です。切り替えは 15ms のランプを通ります。

**Room Tune。** Continuity Camera 経由で iPhone を測定用マイクとして使います。スイープを鳴らし、
はっきりした共振を探して、下げる方向だけを提案し、結果をプリセットにします。これが何ではないかは
下に書きました。

**プリセット。** 内蔵は 25 個。Signature・Apple・Speakers・Headphones に分けてあります。自分の
設定を保存し、JSON でやり取りできます。出力デバイスごとに固定しておけば、ヘッドフォンを挿した
ときにそのカーブが戻ります。

**Now Playing と Inspect。** メニューバーのウィンドウにアートワーク、再生操作、
[LRCLIB](https://lrclib.net) の同期歌詞が出ます。Inspect は読むだけです。ピーク、RMS、
ステレオ幅、相関、サンプルレート、リミッターの動き、ドロップアウト、そしてレイテンシを 2 つ。
デバイスが報告する値と、Roomcut 自身が足す値(リミッターのルックアヘッドと、変換があるときの
リサンプラー)を分けて出します。

表示言語は英語・韓国語・日本語・フランス語・ドイツ語。既定ではシステムの言語に従い、Settings で
選び直せます。

## インストール

[Releases](https://github.com/habinsong/roomcut/releases) から `Roomcut-1.1.0.pkg` を
ダウンロードしてください。`Roomcut-1.1.0.dmg` は同じパッケージをディスクイメージに入れたものです。

このビルドは ad-hoc 署名です。Developer ID 証明書はなく、公証も通していません。起動が止められたら、
一度開いたうえで システム設定 → プライバシーとセキュリティ → **このまま開く** を選んでください。
そのボタンは止められたあとにしか現れません。

Gatekeeper を通したくなければ、ターミナルから入れられます。`installer` は Gatekeeper を見ません。

```sh
sudo installer -pkg Roomcut-1.1.0.pkg -target /
```

「確認できません」ではなく **「壊れている」** と出る場合は別の問題です。ダウンロードが壊れたか、
署名が壊れています。まず手元のファイルを確かめてください。

```sh
shasum -a 256 Roomcut-1.1.0.pkg
# 2f45b09f446f42c3c8f3f7649dccf910f6a629785f25152d045b635b7431d693
shasum -a 256 Roomcut-1.1.0.dmg
# b0337c5df3b7a45c36785d27597d61f67719532fb1b3db2137d2eea110fe1cdb
```

インストーラはアプリを `/Applications` に、ドライバをシステムの HAL フォルダに、バックグラウンドの
エンジンを `/Library/Application Support/Roomcut` の下に置きます。そのあと `coreaudiod` を
再起動するので、Mac の音が 1 秒ほど止まります。Roomcut に Dock アイコンはなく、メニューバーで
開きます。システム設定 → サウンドで **Roomcut Output** を選ぶか、アプリに任せてください。そのあと
実際に音を出すデバイスを指定して、処理をオンにします。

### ソースからビルド

```sh
git clone https://github.com/habinsong/roomcut.git
cd roomcut

cmake -S . -B build -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
bash scripts/build-app.sh release
sudo bash scripts/install-driver.sh
```

Xcode 26 と CMake が要ります。いまのオーディオ設定に触られたくなければ、最後の行を外してください。
そこまでは `build/` の中にしか書きません。

## できないこと

- **Intel Mac と古い macOS。** Apple Silicon と macOS 26 (Tahoe) 以降だけです。
- **マルチチャンネル。** 仮想デバイスはステレオです。サラウンドは macOS 側でダウンミックスされて
  から届きます。
- **アプリごとの処理。** システム出力を丸ごと通すか、使わないかのどちらかです。
- **本気のルーム補正。** Room Tune は部屋の一点で電話のマイクを使って測ります。マイクは平坦では
  ないし、一点は部屋ではありません。結果は出発点であって測定値ではありません。最後は耳で決めてください。
- **App Store 配布。** Now Playing が Apple の非公開 MediaRemote フレームワークを読みます。
  そのため Mac App Store には出せず、macOS の更新でオーディオ経路とは無関係にこのパネルだけが
  壊れることがあります。
- **アプリを終了すれば終わり、という作り。** DSP はバックグラウンドのデーモンで動きます。アプリを
  終了しても音はそこを通り続けます。外すにはアンインストールが要ります。

## 仕組み

```text
システムオーディオ
  → Roomcut Output                 仮想デバイス
  → Roomcut.driver                 CoreAudio Audio Server Plug-in
  → 共有メモリのリングバッファ
  → RoomcutAudioEngine             DSP とレンダリング
  → スピーカー / ヘッドフォン / DAC / HDMI
```

| コンポーネント | 役割 | 言語 |
|---|---|---|
| `Roomcut.app` | メニューバーアプリと Now Playing ウィンドウ | Swift (SwiftUI + AppKit) |
| `Roomcut.driver` | 仮想出力デバイス | C |
| `RoomcutAudioEngine` | DSP とオーディオレンダリング | C++ |
| `RoomcutCore` | DSP、分析、プリセット | C++ |
| `RoomcutNowPlaying.dylib` | Now Playing ブリッジ | Objective-C |

オーディオを持っているのはアプリではなくエンジンです。LaunchDaemon として動き、自分の状態
ファイルを持ち、ドライバの書き込みインデックスとハートビートを一緒に見ます。そうしないと
「何も再生していない」と「ドライバが消えた」を区別できません。2026-08-01 に踏んだ故障のせいで、
監視がもう 1 つ増えました。iFi の DAC が 48kHz で開き、直後に 384kHz で開き直り、そこから
レンダーコールバックが実時間の 3.37 倍で回り、リングが枯れてアンダーランが積み上がりました。
デバイスもサンプルレートも見かけ上は同じなので、どの検査にも引っかかりません。いまはフレームが
実際に出ていく速さを測り、数字がおかしければ出力ユニットを作り直します。

## プライバシー

オーディオは Mac の外に出ません。ログに残るのはカウンタとデバイス名だけで、サンプルは残りません。
Room Tune が iPhone のマイクを開くのは測定中だけです。ネットワークを使うのは歌詞だけで、
タイトル・アーティスト・長さを LRCLIB に送り、返ってきたものを
`~/Library/Caches/com.habinsong.roomcut/lyrics.json` にキャッシュします。

## ビルドとテスト

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift test -c release
cmake -S . -B build -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

このコミットの時点でネイティブ 43 個、Swift 257 個です。ここで扱えないのは実際の部屋、実際の
マイク、そして別の Mac です。領域ごとの検証記録と、まだ開いている限界は
[docs/development](development/README.md) にあります。

## アンインストール

```sh
sudo bash scripts/uninstall-driver.sh
```

エンジンが終了するときにシステムの既定出力を元のデバイスへ戻し、そのあとドライバを削除して
`coreaudiod` を再起動します。それでも無音なら、システム設定 → サウンドで出力を選び直してください。

## ライセンス

Apache License 2.0 です。[LICENSE](../LICENSE) を見てください。帰属表示と商標の注意は
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) にあり、どちらもアプリとインストーラに
入っています。
