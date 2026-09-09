<div align="center">

<img src="../icon/roomcut_icon.png" alt="Roomcut" width="184" height="184">

# Roomcut

[English](../README.md) · [한국어](README.ko.md) · **日本語** · [Français](README.fr.md) · [Deutsch](README.de.md)

[![Download](https://img.shields.io/github/v/release/habinsong/roomcut?style=for-the-badge&label=download&color=2EA043)](https://github.com/habinsong/roomcut/releases/latest) [![License](https://img.shields.io/badge/license-Apache--2.0-D22128?style=for-the-badge)](../LICENSE) ![macOS 26+](https://img.shields.io/badge/macOS-26%2B-000000?style=for-the-badge) ![Apple Silicon](https://img.shields.io/badge/Apple-Silicon-555555?style=for-the-badge) ![Local-first](https://img.shields.io/badge/LOCAL--FIRST-1f2328?style=for-the-badge)

Roomcut 1.0.9 は、macOS 26 以降の Apple Silicon Mac 用です。

</div>

> **公式リポジトリ**
>
> Roomcut は [habinsong](https://github.com/habinsong) が制作・管理しています。このリポジトリで明記していないコピー、ミラー、リブランド、類似プロジェクトは Roomcut と関係ありません。ソースコードは Apache License 2.0 で提供します。Roomcut の名前、ブランド、スクリーンショット、文書は © 2026 송하빈 であり、このライセンスには含まれません。

Roomcut は macOS 全体で使えるオーディオプロセッサです。仮想出力デバイスを作り、
Mac で再生している音を処理してから、スピーカー、ヘッドフォン、DAC へ送ります。
CoreAudio のドライバを直接使うため、BlackHole や Soundflower のような別の
ループバックドライバは必要ありません。

## 聴いている音に合わせた空間感の調整

Focus は広がりすぎたステレオイメージを内側へ寄せ、Space はイメージを広げます。
プリセットから始めて、必要なら各コントロールを手で調整できます。

<div align="center">
<table>
<tr>
<td align="center" valign="middle"><img src="../icon/app/main_home.png" alt="Roomcut ホーム" width="200"><br><sub>ホーム</sub></td>
<td align="center" valign="middle"><img src="../icon/app/menubar.png" alt="Roomcut メニューバー" width="270"><br><sub>メニューバー</sub></td>
<td align="center" valign="middle"><img src="../icon/app/compact_mode.png" alt="Roomcut コンパクトモード" width="220"><br><sub>コンパクトモード</sub></td>
</tr>
</table>
</div>

## 1.0.9 の変更点

- A/B 比較は A と B それぞれの編集履歴を持ち、判断する前に再生レベルを合わせられます。
  切り替えには短いランプを入れ、通常の比較でクリック音が出にくいようにしました。
- 出力の復帰、デバイスの読み取りと書き込み、アプリのポーリングの担当を分けました。
  遅れて届いた応答や古い書き込みが、新しい選択を上書きしないための変更です。
- Room Tune は、キャンセルや遅延が起きた測定ラウンドを片付ける経路を明確にしました。
  一時的なバイパスは、その測定がまだ所有しているときだけ戻します。
- リリースは `.pkg` インストーラと、そのインストーラを入れた `.dmg` の両方で配布します。

## できること

- **EQ と音色調整。** 10 バンドのグラフィック EQ、6 バンドのパラメトリック EQ、
  プリアンプ、出力トリム、リミッター、Bass / Warmth / Vocal / Clarity / Air を使えます。
- **空間の調整。** Focus で像を寄せ、Space で広げます。Center、Damping、
  クロスフィード、スピーカー/ヘッドフォンモード、サラウンドは必要なときだけ使えます。
- **A/B と取り消し。** 二つの音の設定を保存し、一方を他方へコピーしたり、
  レベルを合わせて比べたりできます。一回のドラッグは一回の編集として取り消せます。
- **Room Tune。** Continuity Camera 経由で iPhone をマイクとして使い、部屋を測定します。
  目立つ共振だけを下げる EQ としてプリセットに保存します。校正用マイクの代わりではありません。
- **Now Playing。** メニューバーのウィンドウでアートワーク、再生操作、
  [LRCLIB](https://lrclib.net) の同期歌詞を表示できます。
- **プリセットとデバイス別設定。** 内蔵プリセットから始め、自分の設定を保存し、
  JSON で共有できます。出力デバイスごとにプリセットを覚えさせることもできます。
- **Inspect。** ピーク、RMS、ステレオ幅、相関、サンプルレート、レイテンシ、
  リミッターの動き、ドロップアウトを、音を変えずに確認できます。
- **5 つの表示言語。** 英語、韓国語、日本語、フランス語、ドイツ語に対応します。
  システム言語に従うか、Settings で選べます。

## 仕組み

macOS は仮想デバイス **Roomcut Output** に音を送ります。`Roomcut.driver` は
`coreaudiod` の中で動き、共有リングバッファへフレームを渡します。
`RoomcutAudioEngine` が DSP を適用し、実際の出力デバイスへレンダリングします。

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

## 動作環境

- Apple Silicon
- macOS 26 (Tahoe) 以降
- ソースからビルドする場合は Xcode 26 と CMake

## インストール

### リリースからインストール

[Releases](https://github.com/habinsong/roomcut/releases) から `Roomcut-1.0.9.pkg` または
`Roomcut-1.0.9.dmg` をダウンロードします。ディスクイメージの場合は開いて、中の
パッケージをダブルクリックしてください。インストーラはアプリを `/Applications` に置き、
仮想ドライバとエンジンを入れてから `coreaudiod` を再起動します。音が一瞬止まることがあります。

このビルドは ad-hoc 署名で、公証されていません。macOS に止められた場合はパッケージを
Control-クリックして **開く** を選び、必要なら **プライバシーとセキュリティ** で許可してください。
ターミナルからは次のようにインストールできます。

```sh
sudo installer -pkg Roomcut-1.0.9.pkg -target /
```

Applications から Roomcut を開いてください。Dock には表示されず、メニューバーで動きます。
その後、System Settings → Sound で **Roomcut Output** を選ぶか、アプリに任せます。

### ソースからビルド

```sh
git clone https://github.com/habinsong/roomcut.git
cd roomcut

cmake -S . -B build -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
bash scripts/build-app.sh release
sudo bash scripts/install-driver.sh
```

インストールすると `coreaudiod` が再起動します。今のオーディオ設定を変えたくなければ、
最後のインストールコマンドを省いてビルドだけ行えます。

## 使い方

1. Mac の出力デバイスとして **Roomcut Output** を選びます。
2. Roomcut がレンダリングする実デバイスを選び、処理をオンにします。オフにしてもアプリは
   終了せず、バイパスになるだけです。
3. ヘッドフォンやスピーカーに合うプリセットから始め、聴こえる理由があるところだけ調整します。

5 つのタブは役割を重ねないように分けています。

- **Home**: Now Playing、処理スイッチ、簡単な音色調整、音量、プリセット、展開できる完全な EQ
- **Space**: ステレオ幅、センターフォーカス、ダンピング、クロスフィード、Focus/Widen の出発点
- **Tune**: iPhone の測定と、結果をプリセットとして保存する場所
- **Inspect**: エンジンの状態を変えずに読むメーター
- **Settings**: 出力デバイスと形式、デバイス別プリセット、起動、外観、言語、プリセットファイル、歌詞キャッシュ

音のシートを最後まで開くと **A/B** が表示されます。A と B は独立した編集履歴を持ち、
`⌘Z` と `⇧⌘Z` は選択中の側に作用します。レベルマッチは公平な比較に役立ちますが、
実際の再生信号がないと一致状態は表示できません。

## プライバシーと制限

オーディオ処理と分析は Mac の中で完結します。ログに残るのはオーディオサンプルではなく、
カウンタとデバイス名です。Room Tune が iPhone のマイクを使うのは測定中だけです。歌詞は
タイトル、アーティスト、長さで LRCLIB に問い合わせ、ローカルにキャッシュします。LRCLIB にない曲は表示されないことがあります。

Roomcut は Apple の非公開 MediaRemote フレームワークを通して Now Playing 情報を読みます。
そのため Mac App Store では配布できず、macOS の更新がオーディオ経路とは別に Now Playing 表示へ影響する可能性があります。

## ビルドとテスト

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift test -c release
cmake -S . -B build -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

全ソースの一覧、開発記録、検証記録、残っているハードウェア検証の範囲は
[docs/development](development/README.md) にあります。

## アンインストール

```sh
sudo bash scripts/uninstall-driver.sh
```

可能な場合は以前の出力デバイスを戻し、`coreaudiod` を再起動します。必要なら System Settings →
Sound で出力デバイスを選び直してください。

## ライセンスとクレジット

Roomcut は Apache License 2.0 で提供しています。[LICENSE](../LICENSE) を参照してください。
帰属表示と商標に関する注意は [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) にあり、
両方のファイルはアプリとインストーラに含まれます。
