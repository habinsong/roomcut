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
ボーカルはスライダーを振り切っても手つかずのまま出ます。この条件を満たすため一度設計を一新しました。
当初はミッドの位相を反転させた複製を足して広げていましたが、モノ互換は保てても定位が崩れ、
スライダーを上げるほどボーカルが左へ流れました。Space は ±200、Center と Damping は
200 まで動きます。カーブは変えていないので、いま使っている値はそのままの音です。同じタブに
Crossfeed とスピーカー/ヘッドフォンの切り替えがあります。

**音量を合わせた A/B。** スロットは2つで、編集履歴は独立しています。`⌘Z` と `⇧⌘Z`
は表示中の側に適用され、コピーボタンで現在の設定を反対側へ渡せます。切り替えは 15ms の
ランプを通ります。Level Match を有効にすると、リミッターを含む両系統を同じ区間で
K 特性メーターで測定し、大きい側を下げます。音量差ではなく音質を比較するための機能です。

**Room Tune。** Continuity Camera 経由で iPhone を測定用マイクとして使います。スイープを鳴らし、
はっきりした共振を探して、下げる方向だけを提案し、結果をプリセットにします。想定していない用途は
後述の「できないこと」にまとめました。

**プリセット。** 内蔵は 25 個。Signature・Apple・Speakers・Headphones に分けてあります。自分の
設定を保存し、JSON でやり取りできます。出力デバイスごとに固定しておけば、ヘッドフォンを挿した
ときにそのカーブが戻ります。

**Now Playing と Inspect。** メニューバーのウィンドウにアートワーク、再生操作、
[LRCLIB](https://lrclib.net) の同期歌詞が出ます。Inspect は読むだけです。ピーク、RMS、
ステレオ幅、相関、サンプルレート、リミッターの動き、ドロップアウト、そしてレイテンシを 2 つ。
デバイスが報告する値と、Roomcut 自身が足す値(リミッターのルックアヘッドと、変換があるときの
リサンプラー)を分けて出します。

表示言語は英語と韓国語、日本語、フランス語、ドイツ語です。既定ではシステムの言語に従い、Settings で
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

インストーラはアプリを `/Applications` に、ドライバをシステムの HAL フォルダに、バックグラウンド
エンジンを `/Library/Application Support/Roomcut` 配下に配置します。そのあと `coreaudiod` を
再起動するため、Mac の音が 1 秒ほど途切れます。Roomcut に Dock アイコンはなく、メニューバーから
開きます。システム設定 → サウンドで **Roomcut Output** を選ぶかアプリに任せ、実際に音を鳴らす
出力先を指定して処理を有効にしてください。

### ソースからビルド

```sh
git clone https://github.com/habinsong/roomcut.git
cd roomcut

cmake -S . -B build -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
bash scripts/build-app.sh release
sudo bash scripts/install-driver.sh
```

Xcode 26 と CMake が必要です。現在のオーディオ設定を変更したくない場合は、最後の行を実行しないでください。
そこまでは `build/` 内にのみ書き込みます。

## できないこと

- **Intel Mac と古い macOS。** Apple Silicon と macOS 26 (Tahoe) 以降にのみ対応します。
- **マルチチャンネル。** 仮想デバイスはステレオ専用です。サラウンドは macOS 側でダウンミックスされてから届きます。
- **アプリごとの処理。** システム出力全体にかかります。アプリごとに個別にオン/オフすることはできません。
- **本格的なルーム音響補正。** Room Tune は部屋の一点で電話のマイクを使って測定します。マイクの周波数特性は
  平坦ではなく、一点の測定が部屋全体の音響特性を表すわけでもありません。結果は調整の出発点であり、最終的な判断は耳で行ってください。
- **Mac App Store 配布。** Now Playing が Apple の非公開 MediaRemote フレームワークを参照します。
  そのため Mac App Store には公開できず、将来の macOS アップデートでオーディオ処理とは無関係にこのパネルのみが
  動作しなくなる可能性があります。
- **アプリ終了で処理が止まる動作。** DSP はバックグラウンドのデーモンとして常駐します。アプリを終了しても
  音声はデーモンを通過し続けます。完全に外すにはアンインストールが必要です。

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

オーディオを管理するのはアプリではなくエンジンです。LaunchDaemon として常駐し、独自の状態
ファイルを保持しながら、ドライバの書き込みインデックスとハートビートを常時監視します。これにより
「無音再生」と「ドライバの消失」を確実に切り分けます。2026-08-01 に発生した障害を受けて
監視処理を追加しました。iFi 製 DAC が 48kHz で開いた直後に 384kHz で再オープンされ、
レンダーコールバックが実時間の 3.37 倍で駆動してリングバッファが枯渇し、アンダーランが多発しました。
デバイスもサンプリングレートも見かけ上は変わらなかったため、従来の検査をすり抜けていました。
現在はフレームの実際の送出レートを計測し、異常値を検知すると出力ユニットを自動で再生成します。

## プライバシー

オーディオデータが Mac の外部へ送信されることはありません。ログに残る情報はカウンタとデバイス名のみで、
オーディオサンプルは記録されません。Room Tune が iPhone のマイクを使用するのは測定中のみです。
ネットワーク通信を行うのは歌詞取得機能だけで、曲名・アーティスト名・演奏時間を LRCLIB へ問い合わせ、
取得結果を `~/Library/Caches/com.habinsong.roomcut/lyrics.json` にキャッシュします。

## ビルドとテスト

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift test -c release
cmake -S . -B build -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

このコミット時点でネイティブテスト 43 件、Swift テスト 257 件をパスしています。テスト環境で検証できないのは
実際の部屋の音響、実機マイクのばらつき、および異なるハードウェア構成の Mac です。領域別の検証記録と
既知の制約は [docs/development](development/README.md) にまとめてあります。

## アンインストール

```sh
sudo bash scripts/uninstall-driver.sh
```

エンジンが終了する際にシステムの既定出力を元のデバイスへ戻し、続いてドライバを削除して
`coreaudiod` を再起動します。もし音が鳴らない場合は、システム設定 → サウンドで出力デバイスを再選択してください。

## ライセンス

Apache License 2.0 です。[LICENSE](../LICENSE) を参照してください。帰属表示と商標の注意は
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) に記載しており、どちらもアプリおよびインストーラに
同梱されます。
