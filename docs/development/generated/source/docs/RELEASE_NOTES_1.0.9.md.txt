# Roomcut 1.0.9

Released 2026-09-09.

## English

This release finishes the current A/B comparison path. A and B retain separate
edit histories, their playback level can be matched before listening, and the
switch is ramped instead of stepped.

Device reads, device writes, output recovery, and app polling now keep their
own order and state. A delayed result should not replace a newer device choice
or sound edit. Room Tune also cleans up a cancelled or delayed measurement round
before it returns its temporary bypass.

Download either `Roomcut-1.0.9.pkg` or `Roomcut-1.0.9.dmg`. The disk image holds
the same package. The payload binaries are ad-hoc signed; this release is not
notarized. If macOS blocks the package, Control-click it, choose **Open**, then
approve it in **Privacy & Security** if asked.

Requires Apple Silicon and macOS 26 (Tahoe) or later.

Local checks: native CTest 37/37, Swift XCTest 213/213, app bundle signature
structure, package version, and disk-image checksum all passed. These checks do
not replace an install or upgrade test on another Mac, a physical microphone
measurement, or a listening evaluation.

## 한국어

이번 릴리스에서 A/B 비교 경로를 마무리했습니다. A와 B는 각각 편집 이력을 유지하고,
듣기 전에 재생 레벨을 맞출 수 있습니다. 전환도 계단식으로 바꾸지 않고 짧은 램프를
거칩니다.

장치 읽기·쓰기, 출력 복구, 앱 폴링은 각자 순서와 상태를 관리합니다. 늦게 도착한 결과가
새 장치 선택이나 새 사운드 편집을 덮어쓰지 않도록 한 변경입니다. Room Tune도 취소되거나
지연된 측정 회차를 정리한 뒤 임시 바이패스를 되돌립니다.

`Roomcut-1.0.9.pkg` 또는 `Roomcut-1.0.9.dmg`를 받으면 됩니다. 디스크 이미지는 같은
패키지를 담고 있습니다. 페이로드 바이너리는 ad-hoc 서명이고 이번 릴리스는 공증되지
않았습니다. macOS가 패키지를 막으면 Control-클릭 후 **열기**를 선택하고, 필요하면
**개인정보 보호 및 보안**에서 승인하세요.

Apple Silicon과 macOS 26 (Tahoe) 이상이 필요합니다.

로컬 검사에서 네이티브 CTest 37/37, Swift XCTest 213/213, 앱 번들 서명 구조,
패키지 버전, 디스크 이미지 체크섬을 모두 확인했습니다. 이 범위는 다른 Mac에서의 설치·업그레이드,
실제 마이크 측정, 청취 평가를 대신하지 않습니다.

## 日本語

このリリースで、現在の A/B 比較の経路を仕上げました。A と B はそれぞれ編集履歴を
持ち、聴く前に再生レベルを合わせられます。切り替えも段階的に変えず、短いランプを
通します。

デバイスの読み取りと書き込み、出力の復帰、アプリのポーリングは、それぞれ順序と状態を
管理します。遅れて届いた結果が新しいデバイス選択や音の編集を上書きしないための変更です。
Room Tune も、キャンセルまたは遅延した測定ラウンドを片付けてから一時バイパスを戻します。

`Roomcut-1.0.9.pkg` または `Roomcut-1.0.9.dmg` をダウンロードしてください。ディスク
イメージには同じパッケージが入っています。ペイロードのバイナリは ad-hoc 署名で、この
リリースは公証されていません。macOS がパッケージを止める場合は Control-クリックして
**開く** を選び、必要なら **プライバシーとセキュリティ** で許可してください。

Apple Silicon と macOS 26 (Tahoe) 以降が必要です。

ローカルでは、ネイティブ CTest 37/37、Swift XCTest 213/213、アプリバンドルの署名構造、
パッケージのバージョン、ディスクイメージのチェックサムを確認しました。この範囲は、別の Mac
でのインストールやアップグレード、実際のマイク測定、試聴評価の代わりにはなりません。

## Français

Cette version termine le parcours A/B actuel. A et B gardent chacun leur
historique d'édition, leurs niveaux de lecture peuvent être rapprochés avant
l'écoute, et la bascule passe par une courte rampe plutôt que par un saut.

Les lectures et écritures de périphériques, la reprise de sortie et
l'interrogation de l'app gèrent maintenant leur propre ordre et état. Un résultat
tardif ne doit plus remplacer un choix récent de périphérique ou un réglage
sonore. Room Tune nettoie aussi une mesure annulée ou retardée avant de restaurer
son bypass temporaire.

Téléchargez `Roomcut-1.0.9.pkg` ou `Roomcut-1.0.9.dmg`; l'image disque contient
le même paquet. Les binaires du payload sont signés ad-hoc et cette version n'est
pas notarisée. Si macOS bloque le paquet, faites un Control-clic, choisissez
**Ouvrir**, puis autorisez-le dans **Confidentialité et sécurité** si nécessaire.

Apple Silicon et macOS 26 (Tahoe) ou ultérieur sont requis.

En local, CTest natif 37/37, XCTest Swift 213/213, la structure de signature de
l'app, la version du paquet et la somme de contrôle de l'image disque ont tous
été vérifiés. Cela ne remplace pas une installation ou une mise à niveau sur un
autre Mac, une mesure avec microphone réel ou une écoute.

## Deutsch

Diese Version schließt den aktuellen A/B-Vergleich ab. A und B behalten jeweils
einen eigenen Bearbeitungsverlauf, ihre Wiedergabepegel lassen sich vor dem Hören
angleichen, und der Wechsel läuft über eine kurze Rampe statt über einen Sprung.

Geräte-Lesen, Geräte-Schreiben, Ausgabewiederherstellung und App-Polling verwalten
nun jeweils ihre eigene Reihenfolge und ihren Zustand. Ein verspätetes Ergebnis
soll keine neuere Geräteauswahl oder Klangänderung ersetzen. Room Tune räumt eine
abgebrochene oder verspätete Messrunde auf, bevor der vorübergehende Bypass
zurückgenommen wird.

Laden Sie `Roomcut-1.0.9.pkg` oder `Roomcut-1.0.9.dmg` herunter; das Disk-Image
enthält dasselbe Paket. Die Payload-Binärdateien sind ad-hoc signiert, diese
Version ist nicht notarisiert. Blockiert macOS das Paket, klicken Sie mit Control
darauf, wählen **Öffnen** und erlauben es bei Bedarf unter **Datenschutz & Sicherheit**.

Apple Silicon und macOS 26 (Tahoe) oder neuer sind erforderlich.

Lokal wurden der native CTest mit 37/37, Swift XCTest mit 213/213, die
Signaturstruktur des App-Bundles, die Paketversion und die Prüfsumme des
Disk-Images geprüft. Das ersetzt keinen Installations- oder Upgrade-Test auf
einem anderen Mac, keine physische Mikrofonmessung und keine Hörbeurteilung.
