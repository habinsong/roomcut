# 2026-09-08 바이패스 선택과 측정 복구의 소유권

기존 Settings 조작과 Room Tune 화면은 유지했습니다.
사용자의 직접 선택과 측정의 임시 우회/복구를 구분해 이전 측정이 최신 선택을 덮어쓰지 않도록 했습니다.

## 재현

기존 UI의 시작/복구 동작을 `BypassOverride`로 옮긴 뒤 실제 뷰 모델과 제어 가능한 비동기 클라이언트로 검사했습니다.
2개 테스트의 4개 항목이 실패했습니다. 측정 우회 응답을 보류하고 취소한 다음 사용자가 바이패스를 켜자
요청은 `[true, true, false]`, 최종 값은 false였습니다. 마지막 false가 이전 측정의 복구였습니다.
또한 동시에 시작된 직접 변경이 늦게 끝나 최신 값을 뒤집는 비동기 클라이언트 경계도 재현했습니다.
[수정 전](raw/2026-09-08-bypass-ownership/before.txt).

수정 후 같은 측정 취소/직접 선택 조건은 `[true, true]`, 최종 true입니다.
[수정 후 대상 검사](raw/2026-09-08-bypass-ownership/targeted.txt).
모의 비동기 클라이언트 검사이며 운영 엔진이나 실제 오디오 장치를 변경한 결과가 아닙니다.

## 책임과 동작

| 파일 | 책임 |
|---|---|
| `BypassWriter.swift` | 직접 선택 리비전·임시 소유권·한 건 진행/최신 한 건 대기·현재 요청의 오류/완료 |
| `BypassOverride.swift` | 시작 시점의 이전 상태 기록과 임시 우회/중복 없는 복구. 직접 선택으로 소유권이 바뀌면 이전 복구는 생략 |
| `RoomcutViewModel.swift` | 구성요소 소유·직접 변경의 동기 접수·현재 결과의 상태/오류 게시 |
| `RoomTuneTab.swift` | 임시 범위의 시작/복구를 기존 측정 워크플로에 연결 |
| `BypassOwnershipTests.swift`, `RoomcutViewModelTests.swift` | 실제 뷰 모델을 통한 지연·취소·연속 선택·복구·실패 행렬 |
| `UIFixture.swift`, `SettingsTab.swift` | 평가용 상태의 실제 메모리 값 반영과 평가 모드에서만 요청/응답 추적 |
| `Package.swift` | 새 코어 구성요소 연결 |

직접 선택은 Task가 나중에 실행될 때가 아니라 `setBypass` 호출 때 접수합니다.
이전 요청이 진행 중이면 최신 대기 값 하나만 남깁니다. 전송 전과 완료 처리에서 현재 요청/소유권을 다시 확인합니다.
대기 중인 직접 선택은 오래된 폴링 값보다 우선하여 복구 기준에 반영합니다.

이미 전송된 복구는 뒤로 되돌릴 수 없지만 이후 직접 선택이 마지막에 실행됩니다.
아직 전송되지 않은 이전 복구는 사용자의 직접 변경 뒤에 보내지 않습니다.
소유권을 잃은 범위의 복구 성공은 원래 값 적용을 뜻하지 않고, 최신 사용자 선택에 제어를 넘긴 무동작 완료입니다.
한 범위의 복구는 반복 호출해도 같은 완료를 기다리며, 다른 범위의 소유권을 해제하지 않습니다.

성공 응답을 받지 못했다고 엔진이 바뀌지 않았다고 가정하지 않습니다.
임시 쓰기가 적용된 뒤 오류를 반환한 경우도 이전 상태를 복구하는 검사에 포함했습니다.
현재 직접 쓰기의 오류는 일반 폴링/오래된 성공으로 사라지지 않으며 다음 성공한 요청으로 정리됩니다.

## 검사

| 명령/검사 | 결과 | 자료 |
|---|---|---|
| `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift test -c release --jobs 4` | 211/211 통과 | [전체 Swift](raw/2026-09-08-bypass-ownership/swift-tests.txt) |
| 대상 ThreadSanitizer | 29/29 통과 | [동시성 검사](raw/2026-09-08-bypass-ownership/tsan.txt) |
| 바이패스 소유권 대상 | 10/10 통과 | [대상 검사](raw/2026-09-08-bypass-ownership/expanded-after.txt) |
| 최신 앱 빌드/서명 | 성공 | [실제 오디오 QA 기록](2026-09-08-sound-check.md) |
| `ui-roomtune` UI와 요청/응답 추적 | 직접 선택 유지와 정상 복구 확인 | [5초 지연 추적](raw/2026-09-08-bypass-ownership/ui-trace-5s.txt) |

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift test \
  --scratch-path build/bypass-ownership-audit/swift-tsan --sanitize=thread --jobs 4 \
  --filter 'BypassOwnershipTests|RoomTuneWorkflowTests|RoomTuneRoundTests|RoomcutViewModelTests.testAwaitedBypass'
```

추가한 검사에는 이전 true/false 복구, 대기 중 직접 선택, 이미 전송된 복구 뒤의 새 선택,
100회 변경의 한 건 진행/최신 한 건 대기, 오래된 완료와 최신 실패, 반복/타 범위 복구,
이미 취소된 시작, 적용됐지만 응답이 실패한 임시 쓰기가 포함됩니다.
확대 검사 작성 중 await를 XCTest 동기 autoclosure에 넣은 컴파일 오류가 있었으며,
값을 기다린 뒤 비교하도록 검사 보조 함수를 수정했습니다. [기록](raw/2026-09-08-bypass-ownership/expanded-before.txt).

## 평가용 UI의 검증 조건 정정

`ui-roomtune`은 처음에 `liveBypass`를 상태 응답에 반영하지 않았습니다.
이를 수정했고, [이전 UI 기록](2026-09-08-roomtune-lifecycle.md)의 ON 표시만으로 복구를 판정할 수 없음을 명시했습니다.

수정한 상태에서 첫 UI 시도는 Off 표시 뒤 On으로 돌아왔습니다.
요청 추적을 추가해 확인한 결과, UI를 누를 때 임시 우회가 이미 적용되어 실제 직접 요청은 `bypass=false`였습니다.
따라서 확인하려던 `bypass=true` 입력을 만들지 못한 시도였으며 최신 의도 보존 실패로 세지 않습니다.
[3초 지연 추적](raw/2026-09-08-bypass-ownership/ui-trace-3s.txt).
의도한 입력을 안정적으로 만들기 위해 평가용 응답 지연을 5초로 늘렸습니다. 실제 모드에는 영향이 없습니다.
요청/적용/범위 시작/복구 로그도 `ui-roomtune`에서만 출력하며 실제 모드의 값은 기록하지 않습니다.

5초 지연 상태에서 Tune 시작→준비 중→Settings→처리 끄기를 수행했습니다.
로그에 사용자 `bypass=true`가 첫 응답보다 먼저 기록됐습니다. 이후 엔진 쓰기는 true·true였고
기존 복구는 false를 보내지 않았습니다. 모든 응답 뒤 Settings의 처리 토글이 Off임을 화면으로 확인했습니다.
별도로 처리를 켜고 직접 변경 없이 측정을 끝내면 true 우회→false 복구와 완료 결과, Settings On이 확인됐습니다.
평가용 앱은 종료하고 앱 목록에서 사라진 것을 확인했습니다. 이 검사는 실제 소리 재생 없는 메모리 상태입니다.

## 참고와 남은 범위

[Swift 동시성 문서](https://docs.swift.org/swift-book/LanguageGuide/Concurrency.html)와
[Apple의 액터 재진입 설명](https://developer.apple.com/videos/play/wwdc2021/10133/)을 2026-09-08 확인했습니다.
MainActor 사용만으로 await 사이의 상태 가정이 유지되는 것은 아니므로 명시적 요청 순서와 소유권을 검사했습니다.
TSan 결과는 논리적 경합 검사를 대체하지 않습니다.

보호 범위는 이 앱의 바이패스 쓰기 경로입니다. 다른 프로세스가 엔진에 직접 보낸 명령의 의도까지 추적하지는 않습니다.
이미 시작한 네이티브 호출은 끝날 수 있으며 실제 장치·권한·절전/복귀는 별도 검증입니다.
Swift/앱 코드만 바꿔 네이티브 C/C++ ctest는 반복하지 않았습니다.
전체 목표는 활성 상태이며 다음은 재생 완료 누락/캡처 중단 시 회차를 정리하는 경계입니다.
