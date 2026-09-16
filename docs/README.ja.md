<div align="center">

<img src="../icon/roomcut_icon.png" alt="Roomcut" width="184" height="184">

# Roomcut

[English](../README.md) · [한국어](README.ko.md) · **日本語** · [Français](README.fr.md) · [Deutsch](README.de.md)

[![Download](https://img.shields.io/github/v/release/habinsong/roomcut?style=for-the-badge&label=download&color=2EA043)](https://github.com/habinsong/roomcut/releases/latest) [![License](https://img.shields.io/badge/license-Apache--2.0-D22128?style=for-the-badge)](../LICENSE) ![macOS 26+](https://img.shields.io/badge/macOS-26%2B-000000?style=for-the-badge) ![Apple Silicon](https://img.shields.io/badge/Apple-Silicon-555555?style=for-the-badge)

</div>

> **公式リポジトリ**
>
> Roomcut は [habinsong](https://github.com/habinsong) が制作・管理しています。このリポジトリで明記していないコピー、ミラー、リブランド、類似プロジェクトは Roomcut と関係ありません。ソースコードは Apache License 2.0 で提供します。Roomcut の名前、ブランド、スクリーンショット、文書は © 2026 송하빈 であり、このライセンスには含まれません。

Roomcut は、独自開発の仮想オーディオドライバを通じて Mac の全システム出力を一元制御するネイティブ macOS アプリです。BlackHole などの外部ツールを必要とせず、音楽・動画・会議音声を含むすべての音声をリアルタイム DSP で高品位に補正し、iOS のような洗練された直感的な UI/UX を提供します。

<div align="center">
<table>
<tr>
<td align="center" valign="middle"><img src="../icon/app/main_home.png" alt="Roomcut ホーム" width="200"><br><sub>ホーム</sub></td>
<td align="center" valign="middle"><img src="../icon/app/menubar.png" alt="Roomcut メニューバー" width="270"><br><sub>メニューバー</sub></td>
<td align="center" valign="middle"><img src="../icon/app/compact_mode.png" alt="Roomcut コンパクトモード" width="220"><br><sub>コンパクトモード</sub></td>
</tr>
</table>
</div>

## 主な機能

- **システム全体 EQ**: 10バンドグラフィック EQ ＋ 6バンドパラメトリック EQ（ダイナミック EQ 対応）および直感的な5大マクロノブ（Bass、Warmth、Vocal、Clarity、Air）。
- **ステレオ音場調整 (Stereo Space)**: センターボーカルを保持したままサイド成分の広がりを調整する Focus/Space 機能、ヘッドフォン向け Crossfeed、そして音像を頭の外に出す仮想ルーム（Studio / Living / Hall）。
- **音量自動整合 A/B 比較**: K特性ラウドネスメーターで両スロットの音量を一致させ、音量差に惑わされず純粋な音質差を比較可能。
- **Room Tune**: Continuity Camera 経由で iPhone のマイクを利用し、部屋の定在波・共振を測定してカット EQ を自動生成。
- **25種類の内蔵プリセット**: スピーカー、ヘッドフォン、AirPods など出力先に応じた最適プリセット、カスタム JSON プリセットの保存・共有。
- **Now Playing & Inspect**: メニューバーでの再生制御、[LRCLIB](https://lrclib.net) 同期歌詞表示、Peak/RMS/相関度/遅延時間のリアルタイム計測。
- **多言語対応**: 日本語、English、한국어、Français、Deutsch。

## インストール

[Releases](https://github.com/habinsong/roomcut/releases) から最新の `Roomcut-1.1.0.pkg` または `Roomcut-1.1.0.dmg` をダウンロードしてください。

> **初回起動時の注意 (ad-hoc 署名)**<br>
> 初回起動がブロックされた場合は、**システム設定 → プライバシーとセキュリティ → このまま開く** をクリックしてください。

ターミナルからのインストール:
```sh
sudo installer -pkg Roomcut-1.1.0.pkg -target /
```

### ソースからビルド
```sh
git clone https://github.com/habinsong/roomcut.git
cd roomcut

cmake -S . -B build -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
bash scripts/build-app.sh release
sudo bash scripts/install-driver.sh
```

## システム要件と注意事項

- **対応環境**: Apple Silicon Mac、macOS 26 (Tahoe) 以降。
- **出力形式**: 2チャンネルステレオ専用（マルチチャンネル音声は macOS 側で自動ダウンミックス）。
- **システム全体適用**: システム出力全体にかかります。アプリ個別の適用・除外には対応していません。
- **バックグラウンド常駐**: DSP エンジンはバックグラウンドデーモン（`RoomcutAudioEngine`）として動作するため、ウィンドウを閉じても音声処理は維持されます。
- **プライバシー保護**: 音声データが Mac の外に送信されることはありません。ネットワーク通信は歌詞取得（LRCLIB）のみに限定されます。

## アンインストール

```sh
sudo bash scripts/uninstall-driver.sh
```
既定の出力デバイスを元の機器に戻し、仮想ドライバを削除して CoreAudio を再起動します。

## ライセンス

Apache License 2.0。[LICENSE](../LICENSE) および [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) を参照してください。
