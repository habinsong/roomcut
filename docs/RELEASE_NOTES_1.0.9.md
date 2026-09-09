# Roomcut 1.0.9

Released 2026-09-09.

## English

A and B hold two different settings and you can switch between them while music
is playing; turn on **Level** and Roomcut measures both on the same passage and
pulls the louder one down, so switching does not change the volume too.

Each slot keeps its own edit history: ⌘Z undoes, ⇧⌘Z redoes, and the copy button
hands the current settings to the other slot.

The engine and the app were split into modules, which fixed a late device reply
overwriting the device or setting you had just picked, output that did not come
back after a device disappeared, and a cancelled Room Tune measurement leaving
its temporary bypass in place.

Sample-rate conversion moved from the old cubic resampler to a windowed sinc one.

Install from `Roomcut-1.0.9.pkg`, or from `Roomcut-1.0.9.dmg`, which holds the
same package (Apple Silicon, macOS 26 Tahoe or later); the build is not
notarized, so if macOS blocks it, Control-click and choose **Open**.

## 한국어

A와 B에 서로 다른 설정을 담아 재생 중에 바꿔 들을 수 있고, **레벨**을 켜면 같은
구간으로 양쪽을 재서 큰 쪽을 낮추기 때문에 전환할 때 음량까지 달라지지 않습니다.

편집 이력은 A와 B가 따로 쌓입니다. ⌘Z로 되돌리고, ⇧⌘Z로 다시 실행하고, 복사
버튼으로 지금 설정을 반대쪽에 넘길 수 있습니다.

엔진과 앱을 모듈로 나누면서 늦게 도착한 장치 응답이 방금 고른 장치나 설정을
덮어쓰던 문제, 장치가 사라진 뒤 출력이 돌아오지 않던 문제, Room Tune 측정을
취소해도 임시 바이패스가 남던 문제를 고쳤습니다.

샘플레이트 변환기는 기존 3차 보간 대신 창함수 sinc 방식으로 바꿨습니다.

설치는 `Roomcut-1.0.9.pkg`나 같은 패키지가 담긴 `Roomcut-1.0.9.dmg`로 하면 되고
(Apple Silicon, macOS 26 Tahoe 이상), 공증을 받지 않은 빌드라 macOS가 막으면
Control-클릭 후 **열기**를 선택하세요.

## 日本語

A と B に別々の設定を置いて再生中に切り替えられ、**レベル**を入れると同じ区間で
両方を測って大きいほうを下げるので、切り替えで音量まで変わることはありません。

編集履歴は A と B で別々に残ります。⌘Z で取り消し、⇧⌘Z でやり直し、コピー
ボタンで今の設定をもう一方に渡せます。

エンジンとアプリをモジュールに分け、遅れて届いたデバイス応答が選び直した装置や
設定を上書きする問題、デバイスが消えたあとに出力が戻らない問題、Room Tune の
測定を取り消しても一時バイパスが残る問題を直しました。

サンプルレート変換は、従来の 3 次補間から窓関数付き sinc に変えました。

インストールは `Roomcut-1.0.9.pkg`、または同じパッケージが入った
`Roomcut-1.0.9.dmg` から行い（Apple Silicon、macOS 26 Tahoe 以降）、公証を
受けていないビルドなので macOS が止めた場合は Control-クリックして**開く**を
選んでください。

## Français

A et B contiennent deux réglages différents et vous passez de l'un à l'autre
pendant la lecture ; avec **Niveau**, Roomcut mesure les deux sur le même passage
et baisse le plus fort, pour que le changement ne porte pas aussi sur le volume.

Chaque emplacement garde son propre historique : ⌘Z annule, ⇧⌘Z rétablit, et le
bouton de copie envoie le réglage courant vers l'autre.

Le moteur et l'app ont été découpés en modules, ce qui corrige une réponse de
périphérique arrivée en retard qui écrasait l'appareil ou le réglage tout juste
choisi, une sortie qui ne revenait pas après la disparition d'un périphérique, et
une mesure Room Tune annulée qui laissait son bypass temporaire actif.

La conversion de fréquence d'échantillonnage passe de l'ancien rééchantillonneur
cubique à un sinc fenêtré.

Installez depuis `Roomcut-1.0.9.pkg`, ou depuis `Roomcut-1.0.9.dmg` qui contient
le même paquet (Apple Silicon, macOS 26 Tahoe ou ultérieur) ; la build n'est pas
notarisée, donc si macOS la bloque, faites un Control-clic et choisissez
**Ouvrir**.

## Deutsch

A und B halten zwei verschiedene Einstellungen, zwischen denen Sie während der
Wiedergabe wechseln können; mit **Pegel** misst Roomcut beide an derselben Stelle
und senkt die lautere ab, damit sich beim Wechsel nicht auch die Lautstärke ändert.

Jeder Platz führt seinen eigenen Bearbeitungsverlauf: ⌘Z nimmt zurück, ⇧⌘Z stellt
wieder her, und die Kopiertaste gibt die aktuelle Einstellung an den anderen Platz.

Engine und App wurden in Module zerlegt; behoben sind damit eine verspätete
Geräteantwort, die das eben gewählte Gerät oder die eben geänderte Einstellung
überschrieb, eine Ausgabe, die nach dem Verschwinden eines Geräts nicht
zurückkam, und ein abgebrochener Room-Tune-Durchlauf, der seinen vorübergehenden
Bypass stehen ließ.

Die Abtastratenwandlung nutzt statt des bisherigen kubischen Resamplers ein
fenstergewichtetes Sinc-Verfahren.

Installieren Sie über `Roomcut-1.0.9.pkg` oder über `Roomcut-1.0.9.dmg` mit
demselben Paket (Apple Silicon, macOS 26 Tahoe oder neuer); die Builds sind nicht
notarisiert — blockiert macOS sie, Control-Klick und **Öffnen** wählen.
