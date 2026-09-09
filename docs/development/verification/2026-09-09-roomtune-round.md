# 2026-09-09 Room Tune 회차의 중단·기한·정리

RT-05의 남은 범위였던 재생 완료 콜백 누락, 측정 중 캡처/재생 중단, 정리 기한을
제어 가능한 전송으로 검사했습니다. 화면 구성과 측정 순서는 바꾸지 않았습니다.
이 검사의 오디오 장치는 모의 구현이며, 실제 마이크·출력 장치의 중단을 재현한 것이 아닙니다.

## 재현과 수정

녹음이 남지 않은 회차는 `RoomTuneRoundError.recordingFailed`가 아니라 도메인
`Roomcut.RoomTune`의 일반 오류를 던졌습니다. 워크플로가 `localizedDescription`을 그대로
표시하므로, 열거형에 정의된 안내 대신 내부 문자열이 사용자에게 보였습니다.
`testLostRecordingReportsTheRecordingFailure`가 수정 전 실패(`nil` ≠ `recordingFailed`),
수정 후 통과합니다.

중단·기한 정책 자체는 기존 구현이 이미 만족했고, 이번에 검사로 고정했습니다.
검사가 처음부터 통과했다는 사실을 결함이 없다는 증거로 확대하지 않습니다.

## 검사한 조건

| 검사 | 조건 | 확인한 결과 |
|---|---|---|
| `testLostMicrophoneEndsTheRoundWithTheCaptureMessage` | 재생 시작 뒤 상태 검사가 `captureInterrupted`를 던짐 | 5초 안에 종료, 전송의 오류 그대로 전달, `stop` 1회, 임시 파일 삭제 |
| `testLostOutputEndsTheRoundWithThePlaybackMessage` | 같은 조건의 `playbackInterrupted` | 동일. 늦게 도착한 완료 콜백은 `stop`을 다시 부르지 않음 |
| `testPlaybackCompletionAfterTheDeadlineChangesNothing` | 기한 200ms, 완료 콜백은 기한 뒤 2회 | `timedOut`, 분석 미실행, 이벤트는 `prepare`·`play`·`stop`에서 그대로 |
| `testRoundReturnsOnlyAfterASlowStopHasFinished` | 기한 100ms, 네이티브 `stop`이 400ms 지연 | 회차는 400ms 이상 뒤 반환. 정리 전에 제어를 넘기지 않음 |
| `testLostRecordingReportsTheRecordingFailure` | `stop`이 URL을 반환하지 않음 | `recordingFailed`, 임시 파일 삭제 |

기존 회차 검사 9개는 그대로 통과합니다. 기한은 검사에서 주입하며 제품 기본값
(`RoomTuneSignal.duration + 5`초)은 `testMissingPlaybackCompletionEndsAndCleansUpWithoutUserCancellation`이
실제 값으로 계속 확인합니다.

## 한계

- 이미 시작한 동기 네이티브 호출을 선점하지 않습니다. `stop`이 오래 걸리면 회차는 기다립니다.
  이는 다음 측정이 이전 장치를 잡은 채 시작하지 않게 하는 RT-03의 소유권 계약입니다.
- 실제 마이크 분리, 권한 회수, 출력 장치 소실은 검사하지 않았습니다. RT-01·HW-01로 남습니다.
- 평가용 UI나 실기기 측정으로 확장하지 않았습니다.

## 실행 결과

- Swift XCTest 218/218 통과 (회차 검사 14개, 이번에 5개 추가).
- 네이티브 CTest 37/37 통과.
