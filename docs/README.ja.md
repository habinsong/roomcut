<div align="center">

<img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/roomcut_icon.png" alt="Roomcut" width="184" height="184">

# Roomcut

**部屋鳴りを抑え、音はそのままに。**

[English](../README.md) · [한국어](README.ko.md) · **日本語** · [Français](README.fr.md) · [Deutsch](README.de.md)

[![Download](https://img.shields.io/github/v/release/habinsong/roomcut?style=for-the-badge&label=download&color=2EA043)](https://github.com/habinsong/roomcut/releases/latest) [![License](https://img.shields.io/badge/license-Apache--2.0-D22128?style=for-the-badge)](../LICENSE) ![macOS 26+](https://img.shields.io/badge/macOS-26%2B-000000?style=for-the-badge) ![Apple Silicon](https://img.shields.io/badge/Apple-Silicon-555555?style=for-the-badge) ![Local-first](https://img.shields.io/badge/LOCAL--FIRST-1f2328?style=for-the-badge) ![Core Audio plug-in](https://img.shields.io/badge/Core%20Audio-Plug--In-8957E5?style=for-the-badge)

![System-wide](https://img.shields.io/badge/System--wide-EQ%20%2B%20DSP-0A84FF?style=for-the-badge) ![EQ](https://img.shields.io/badge/EQ-10--band%20%2B%20Parametric-5E5CE6?style=for-the-badge) ![Spatial](https://img.shields.io/badge/Spatial-narrow%20%C2%B7%20widen-1F6FEB?style=for-the-badge) ![Room Tune](https://img.shields.io/badge/Room%20Tune-iPhone%20mic-2EA043?style=for-the-badge) ![Now Playing](https://img.shields.io/badge/Now%20Playing-Lyrics-C2185B?style=for-the-badge) ![UI languages](https://img.shields.io/badge/UI-5%20languages-FB8500?style=for-the-badge)

</div>

> **公式リポジトリのお知らせ**
>
> これは **Roomcut** の公式リポジトリであり、[habinsong](https://github.com/habinsong) が作成・管理しています。
>
> このリポジトリの元のドキュメント、README の文章、アーキテクチャの説明、機能の説明、スクリーンショット、UI のコンセプト、プロジェクトのメタデータ、その他の独自素材を複製・ミラー・リブランド、または類似して模倣したリポジトリ、パッケージ、マーケットプレイスの掲載、ウェブサイト、サービス、その他のプロジェクトは、このリポジトリに明記されない限り本プロジェクトとは関係ありません。
>
> ソースコードは Apache License 2.0 で配布されます（[LICENSE](../LICENSE) を参照）。Roomcut の名称、ブランディング、スクリーンショット、ドキュメントは © 2026 송하빈 であり、このライセンスの対象外です。

Roomcut は macOS 向けのシステム全体オーディオプロセッサです。仮想出力デバイスを追加し、Mac が再生するすべての音をリアルタイムの DSP チェーンに通してから、実際に使うスピーカー・ヘッドフォン・DAC へ出力します。ネイティブの CoreAudio Audio Server Plug-in 上で動作するため、BlackHole や Soundflower のような別のループバックドライバは不要です。

多くの「オーディオエンハンサー」は音を外側へ広げるだけです。Roomcut は両方向に動かせて、その度合いも自分で決められます。Focus は過剰な部屋鳴りとステレオの広がりを抑えてボーカルやセリフを原音に近づけ、Widen はより広いステージが欲しいときに広げます。狭めても、広げても、その間のどこにでも合わせられます。

---
#### ひとことで言えば、Roomcut は耳に楽しい音で音楽を聴きながら、<br> Apple の Liquid Glass ベースの UI/UX で、見ているだけで美しいアプリです。
---

<div align="center">
<table>
<tr>
<td align="center" valign="middle"><img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_home.png" alt="メイン画面" width="200"><br><sub>メイン画面</sub></td>
<td align="center" valign="middle"><img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/menubar.png" alt="メニューバー" width="270"><br><sub>メニューバー</sub></td>
<td align="center" valign="middle"><img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/compact_mode.png" alt="コンパクト表示" width="220"><br><sub>コンパクト表示</sub></td>
</tr>
</table>
</div>

## 機能

- **グローバル EQ。** 10 バンドのグラフィック EQ と 6 バンドのパラメトリック EQ（ベル、シェルフ、ハイ/ローパス、ノッチ）を重ねて使え、応答カーブをリアルタイムに表示します。
- **マクロ。** Bass・Warmth・Vocal・Clarity・Air のノブが適切なバンドを代わりに動かすので、周波数を一つひとつ考える必要はありません。
- **双方向の空間コントロール。** 音像を狭めることも広げることも自由に: Space（幅）、Center（ファントムセンターのフォーカス）、Damping（部屋鳴りの低減）、クロスフィード/クロストーク。スピーカー/ヘッドフォンの切り替えとサラウンドのオン・オフができ、スライダーを動かすとライブのステレオフィールド表示が反応します。Focus・Widen はワンタップのプリセット。
- **リミッターとゲイン。** プリアンプ・出力トリムとピークリミッターで、強い EQ カーブでも出力がクリップしないようにします。
- **ハイレゾ対応。** 内部を 32-bit float で処理し、出力デバイスのサンプルレートとビット深度を選べます。Now Playing カードに現在のフォーマットとレイテンシを表示します。
- **アナライザー。** リアルタイムのピーク、RMS、ステレオ幅、スペクトル重心、スペクトル表示、そして今鳴っている音を表す分かりやすいラベル。
- **プリセット。** Signature・Apple 製品・Speakers・Headphones に分類された内蔵ライブラリと、自分で保存したプリセット。
- **Room Tune。** iPhone を Continuity マイクとして使って部屋を測定し、最も目立つ共振に対して控えめな EQ 補正を提案します。下げるだけで上げることはなく、結果をプリセットとして保存します。
- **Now Playing。** アートワーク連動のテーマ、[LRCLIB](https://lrclib.net) による同期歌詞、メニューバーウインドウの再生コントロール。
- **多言語 UI。** 英語・韓国語・日本語・フランス語・ドイツ語。システム言語に従うか、手動で選べます。
- **安全なフォールバック。** エンジンがクラッシュしても出力は実デバイスへ復帰し、元の音声が Mac の外に出ることはありません。

## 仕組み

macOS は仮想デバイス「Roomcut Output」へ音声を送ります。ドライバは薄い部分です。`coreaudiod` の中で動き、DSP は行わず、入ってきたフレームを共有リングバッファ経由でヘルパープロセスへ渡すだけです。ヘルパーである `RoomcutAudioEngine` が DSP チェーンを実行し、実際の出力デバイスへレンダリングします。

```
システムオーディオ
  → Roomcut Output            仮想デバイス
  → Roomcut.driver            Audio Server Plug-in、coreaudiod 内でサンドボックス
  → 共有メモリのリングバッファ Mach サービス経由で受け渡し
  → RoomcutAudioEngine        DSP + レンダリング
  → スピーカー / AirPods / DAC / HDMI
```

Audio Server Plug-in は `coreaudiod` の中でサンドボックス動作し、任意のソケットや共有メモリを開けません。Roomcut は `AudioServerPlugIn_MachServices` 接続を通じてリングバッファをブートストラップします。これは Background Music と同じ方式です。

| コンポーネント | 役割 | 言語 |
|---|---|---|
| `Roomcut.app` | メニューバーアプリと Now Playing の UI | Swift (SwiftUI + AppKit) |
| `Roomcut.driver` | 仮想出力デバイス (Audio Server Plug-in) | C |
| `RoomcutAudioEngine` | バックグラウンドヘルパー: DSP とレンダリング | C++ |
| `RoomcutCore` | 共有 DSP・プリセット・解析 | C++ |
| `RoomcutNowPlaying.dylib` | Now Playing 用 MediaRemote ブリッジ | Objective-C |

## 動作環境

- macOS 26 (Tahoe) 以降。インターフェースがシステムの Liquid Glass API を使用します。
- Apple Silicon。
- ビルド時: Xcode 26（macOS 26 SDK 同梱）と CMake。

## インストール

方法は 2 つあります。今のところソースからのビルドをおすすめします。ダウンロードした未公証バイナリで出る Gatekeeper の警告を避けられるためです。

### ソースからビルド

```sh
git clone https://github.com/habinsong/roomcut.git
cd roomcut

# ネイティブのドライバ + エンジン
cmake -S . -B build
cmake --build build

# メニューバーアプリ
bash scripts/build-app.sh

# ドライバ + エンジンをインストール（パスワードを尋ね、coreaudiod を再起動）
sudo bash scripts/install-driver.sh
```

`build/Roomcut.app` を開きます（メニューバーにあり、Dock アイコンはありません）。続いて System Settings ▸ Sound で「Roomcut Output」を選ぶか、アプリに設定させます。インストール時に `coreaudiod` が再起動するため、システムの音が 1 秒ほど途切れます。

### ビルド済みリリース

[Releases](https://github.com/habinsong/roomcut/releases) から最新の `.pkg`（ダブルクリック）または `.zip`（ターミナル）を入手します。ビルド済みは現在 ad-hoc 署名で公証されていないため、初回起動時に macOS が承認を求めることがあります。`.pkg` を右クリックして「開く」を選ぶか、`sudo installer -pkg Roomcut-*.pkg -target /` を実行してください。`.zip` には残りを処理する `install.sh` が含まれています。ソースからビルドすればこの承認手順はありません。

## 使い方

ドライバをインストールしてアプリを開いた状態で:

1. System Settings ▸ Sound で **Roomcut Output** を選びます（またはアプリに設定させます）。これで Mac が再生するすべての音が Roomcut を通ります。
2. Roomcut がレンダリングする実デバイス（スピーカー・ヘッドフォン・DAC）を決めて、Roomcut をオンにします。オフはクリーンなバイパスです。
3. 機器に合うプリセットから始めて、細かく調整します。

ウインドウには 5 つのタブがあります:

- **Home.** Now Playing、オン/オフのトグル、上にドラッグして開くサウンドコントロールのシート: Bass / Warmth / Vocal / Clarity / Air のマクロ、音量、プリセット選択。さらにドラッグすると 10 バンドとパラメトリックの EQ 全体が現れます。
- **Space.** ステレオ音像を狭めたり広げたり、センターフォーカス・部屋鳴りのダンピング・クロスフィードを設定します。スピーカー・ヘッドフォン向けの Focus / Widen プリセット付き。
- **Tune.** iPhone（Continuity マイク）で部屋を測定し、控えめな EQ 補正を適用します。
- **Inspect.** 読み取り専用のメーター: ピーク、リミッター、ドロップアウト、相関、幅、サンプルレート、レイテンシ、エンジンの状態。
- **Settings.** 出力デバイスとフォーマット、音量、ログイン時に起動、外観（テーマ・レイアウト・言語）、歌詞キャッシュ。

<div align="center">
<img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_space.png" alt="Space" width="150"> <img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_tune_start.png" alt="Tune" width="150"> <img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_tune_measuring.png" alt="Tune 測定中" width="150"> <img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_tune_result.png" alt="Tune 結果" width="150"> <img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_inspect.png" alt="Inspect" width="150">
</div>

ウインドウは任意の辺・角からサイズ変更でき、Home では ↑ / ↓ キーでサウンドコントロールのシートを開閉します。

上部の細いハンドルバーでウインドウを移動します。一度クリックすると全体ウインドウがコンパクトな Now Playing カードに畳まれ、コンパクト表示でハンドルをもう一度クリックするとウインドウをしまいます（Roomcut はメニューバーで動き続け、終了はしません）。カードをタップすると元に戻ります。そのバーのトグルをオンにすると、ウインドウが他のすべてのアプリより前面に固定され、隠れなくなります。Roomcut はメニューバーにも、再生コントロール付きの小さな Now Playing ポップオーバーとして存在します。

### Basic と Advanced

Home のサウンドコントロールのシートには 2 つのモードがあります:

- **Basic** はシンプルに: 5 つのマクロノブ（Bass・Warmth・Vocal・Clarity・Air）、100% を超えて 200% まで上げられる音量スライダー、EQ サマリーのカーブ、プリセット選択。
- **Advanced** は 5 つのサブタブで全機能を開きます:
  - **graph.** 合成された EQ 応答を読み取り専用のカーブ 1 本で表示。
  - **10-Band.** 定番のグラフィック EQ。各バンドを手でドラッグ。
  - **Parametric.** 6 つのバイクアッドバンド（ベル、シェルフ、ハイ/ローパス、ノッチ）を周波数・ゲイン・Q で調整。
  - **Limiter.** ピークリミッターとプリアンプ・出力ゲイン。
  - **Analyzer.** リアルタイムのスペクトルとピーク・RMS・ステレオ幅・スペクトル重心。

<div align="center">
<img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_home_basic.png" alt="Basic コントロール" width="240"> <img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_home_advanced.png" alt="Advanced コントロール" width="240">
</div>

### 外観

Settings ▸ Appearance には 3 つのすばやい切り替えがあります:

- **Auto / Light / Dark.** システムに従うか、ライト/ダークのテーマを強制します。
- **Halo / Cover / Mesh Gradient.** Now Playing の背景: カード周りの穏やかなグローリング（Halo）、ウインドウ全体を満たすアルバムアート（Cover）、アートワークの色から描くアニメーションのメッシュグラデーション（Mesh Gradient）。
- **Card / Poster.** Now Playing のレイアウト: 中央の単一カード（Card）か、端まで広がるカバー（Poster）。

<div align="center">
<img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_settings.png" alt="Settings · 外観" width="240">
</div>

## アンインストール

```sh
sudo bash scripts/uninstall-driver.sh
```

ドライバとエンジンを削除し、以前の出力デバイスを復元してから `coreaudiod` を再起動します。その後も音が出ない場合は、System Settings ▸ Sound で手動でデバイスを選んでください。

## トラブルシューティング

**「Roomcut Output」が Sound 設定に出てこない。** ドライバは `coreaudiod` の再起動時に読み込まれます。`sudo bash scripts/install-driver.sh` を再実行するか（再起動も行います）、Mac を再起動してください。ダウンロードしたドライバは隔離（quarantine）されていてはいけませんが、`.pkg` と `install.sh` がこれを解除します。

**インストール後に音が出ない。** Roomcut の出力先となる実デバイスが設定されているか、Roomcut がオンになっているか確認してください。それでも無音なら `sudo bash scripts/reset-audio-output.sh` を実行するか、System Settings ▸ Sound で手動でデバイスを選んでください。

**アプリが開かない（「開発元が未確認」）。** ビルド済みはまだ公証されていません。アプリを右クリックして「開く」を選ぶか、System Settings ▸ Privacy & Security で許可してください。ソースからビルドすればこの問題はありません。

**ある曲の歌詞が表示されない。** 歌詞はタイトル・アーティスト・長さで照合して LRCLIB から取得します。macOS はストリーミングアプリ自身の歌詞を外部に渡さないため、Apple Music には歌詞があっても LRCLIB になければここでは表示されないことがあります。

**空間コントロールがグレーアウトしている。** それらをサポートするエンジンビルドが必要です。ソースから最新のエンジンを再インストールしてください。

## プライバシー

Roomcut はユーザーが聞くすべての音を処理するため、境界が重要です。元の音声がマシンから出ることはなく、DSP と解析はローカルで実行され、ログにはサンプルではなくカウンターやデバイス名のみが残ります。Room Tune は測定中のみ iPhone のマイクを使用します。

## ビルドとテスト

```sh
swift test                          # アプリ / Swift のユニットテスト
ctest --test-dir build --output-on-failure   # ネイティブ（DSP / エンジン）のテスト
```

## コントリビュート

Issue と Pull Request を歓迎します。上の手順で作業ツリーを用意できます。何かに取りかかるなら、先に Issue を立てるとたいてい皆の時間を節約できます。

## 歌詞とクレジット

トラック情報（タイトル・アーティスト・アートワーク）はシステムの Now Playing メタデータから取得します。同期歌詞はタイトル・アーティスト・長さで [LRCLIB](https://lrclib.net) から照合し、必要に応じて取得してローカルにキャッシュします。Roomcut は `User-Agent` で自分を名乗り、このリポジトリに歌詞を同梱したり再配布したりしません。歌詞はそれぞれの権利者に帰属します。

macOS はストリーミングアプリのアプリ内歌詞を他のアプリに渡さないため、Roomcut は LRCLIB から独自に取得します。ある曲は Apple Music には歌詞があっても、LRCLIB にまだなければここでは表示されないことがあります。

LRCLIB のサーバーとクライアントは MIT ライセンスで、Roomcut は公開 HTTP API 経由でのみ通信するため、そのコードはここには含まれません。

## ライセンス

Roomcut は Apache License 2.0 で配布されます。全文は [LICENSE](../LICENSE) を参照してください。このライセンスはソースコードに適用され、Roomcut の名称とブランディングは対象外です（Apache 2.0 は商標権を付与しません）。
