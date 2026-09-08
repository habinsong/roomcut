# 2026-09-08 기본 출력·형식·밸런스 반환값

기존 화면·슬라이더·편집 이력의 구성은 유지했습니다. 실제 C 진입점과 Swift 호출 경계에서
이미 적용된 결과를 오류로 표시하거나 실패한 쓰기를 성공으로 보고하는 경로를 재현해 수정했습니다.

## 재현

| 경계 | 이전 실행 결과 | 수정 후 결과 |
|---|---|---|
| 이미 선택된 기본 출력 | C는 1을 반환하고 쓰기를 생략하지만 Swift는 `transport(1)`과 오류 문구를 게시 | Swift가 0·1을 성공으로 처리. 음수·알 수 없는 2는 계속 오류 |
| 실장치 명목 샘플레이트 거절 | 반환 0, 물리 형식 96kHz·실장치 48kHz·가상장치 96kHz | 반환 -4, 후속 가상장치 쓰기 중단. 먼저 적용된 물리 형식은 96kHz로 남음 |
| 가상장치 명목 샘플레이트 거절 | 실장치 단계 성공 때문에 전체 성공 0 | 반환 -5. 부분 적용은 그대로 관찰 가능 |
| 두 채널 모두 무음에서 pan=0.5 | 좌우 0이 0.5·1.0으로 상승 | 좌우 0 유지 |
| 채널 읽기 실패·잘린 값·NaN | 최대 레벨이나 잘못된 값을 기준으로 쓰기 진행 | 쓰기 전에 거절. 잘못된 읽기는 호출자의 출력 인자를 덮어쓰지 않음 |
| 한 채널 쓰기 비지원 | 다른 채널에는 쓰기가 발생 | 양쪽 쓰기 가능 여부를 확인한 뒤 진행 |
| 첫 채널 쓰기 실패 | 두 번째 채널도 변경 | 후속 쓰기 중단 |
| 선택 장치 소실·엔진 상태 조회 실패 | 가상장치에 밸런스 쓰기 | 장치를 임의로 바꾸지 않고 오류 반환 |

[수정 전 C 검사](raw/2026-09-08-device-control-results/native-before.txt)는 16개 항목이 실패했습니다.
[추가한 장치 선택 검사](raw/2026-09-08-device-control-results/selection-before.txt)는 2개 항목이 실패했습니다.
[수정 전 Swift 검사](raw/2026-09-08-device-control-results/swift-before.txt)는 3개 검증 항목이 실패했습니다.
항목 수는 독립적인 결함 건수가 아니라 실행한 검증 식의 실패 수입니다.
[수정 후 C 관찰값](raw/2026-09-08-device-control-results/outcomes.txt)을 함께 보존했습니다.

## 변경 파일과 책임

| 파일 | 책임 |
|---|---|
| `engine/client/ClientShim.cpp` | 채널 읽기 검증·양쪽 쓰기 사전 확인·실패 후 중단·알 수 없는 출력 거절·각 샘플레이트 쓰기의 반환값 처리 |
| `engine/client/include/roomcut_client.h` | 부분 적용·비동기 완료·무음 보존·출력 상태 필요 조건 명시 |
| `apps/macos/Roomcut/EngineClient.swift` | 기본 출력의 0·1 성공 계약. 내부 생성자에서 해당 네이티브 함수만 교체해 실제 비동기 Swift 호출 경계를 검사 |
| `apps/macos/RoomcutTests/DeviceCommandTests.swift` | 양 방향 기본 출력·이미 선택됨·실패 코드·불필요한 오류 게시 검사 |
| `engine/tests/ClientDeviceFixture.hpp` | 기존 실제 Mach 서비스와 모의 HAL을 두 C 검사에서 공유. 운영 장치 IO 없음 |
| `engine/tests/test_client_devices.cpp`, `test_client_volume.cpp`, `core/tests/CMakeLists.txt` | 새 장치 명령 검사와 기존 볼륨 회귀를 함께 실행 |

테스트용 내부 생성자는 공개 API가 아닙니다. 운영 `LiveEngineClient()`는 기존 C 함수를 그대로 호출하며,
기본 출력 쓰기는 기존 장치 쓰기 큐에서 실행합니다. 검사에서 실제 메인 스레드 밖 호출도 확인했습니다.
공통 모의 HAL을 옮긴 뒤 기존 `client_volume` 검사도 그대로 통과했습니다.

정상 경로로 기본 출력 변경/반복 요청, ±1 밸런스와 범위 제한, 0.25→-0.5 왕복, 현재 레벨 보존,
지원되는 형식과 실장치 단독 설정, 출력 미선택 상태의 가상장치 밸런스를 검사했습니다.
유효하지 않은 샘플레이트/비트 깊이와 비정상 pan은 장치 쓰기를 발생시키지 않습니다.

## 검증 결과

| 명령·검사 | 결과 | 자료 |
|---|---|---|
| `cmake -S . -B build`, `cmake --build build -j 4` | 성공 | [네이티브 빌드](raw/2026-09-08-device-control-results/native-build.txt) |
| `ctest --test-dir build --output-on-failure` | 37/37 통과 | [전체 호스트](raw/2026-09-08-device-control-results/ctest.txt) |
| 대상 `DeviceCommandTests` | 14/14 통과 | [Swift 대상 검사](raw/2026-09-08-device-control-results/swift-targeted.txt) |
| `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift test -c release --jobs 4` | 175/175 통과 | [전체 Swift](raw/2026-09-08-device-control-results/swift-tests.txt) |
| ASan·UBSan의 `client_volume`, `client_devices` | 2/2 통과 | [계측 검사](raw/2026-09-08-device-control-results/asan-test.txt) |
| `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer bash scripts/build-app.sh release` | 최신 앱 조립 성공 | [앱 빌드](raw/2026-09-08-device-control-results/app-build.txt) |
| `codesign --verify --deep --strict build/Roomcut.app` | 종료값 0 | 로컬 서명 무결성 검사이며 공증·배포 검증 아님 |

계측 명령:

```sh
cmake -S . -B build/client-device-audit/asan -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build build/client-device-audit/asan --target test_client_devices test_client_volume -j 4
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/client-device-audit/asan -R '^client_(volume|devices)$' -V
```

## 출처와 남은 한계

[Apple AudioObjectSetPropertyData](https://developer.apple.com/documentation/coreaudio/audioobjectsetpropertydata(_:_:_:_:_:_:)),
[기본 출력 속성](https://developer.apple.com/documentation/coreaudio/kaudiohardwarepropertydefaultoutputdevice),
[명목 샘플레이트 속성](https://developer.apple.com/documentation/coreaudio/kaudiodevicepropertynominalsamplerate)을 2026-09-08 확인했습니다.
쓰기 API의 공식 Markdown 본문을 읽어 반환값과 HAL 반영 시점이 별개임을 재확인했습니다.

C 함수와 Mach는 실제 구현이며 장치와 엔진 응답은 모의 값입니다.
[실행 파일 연결 목록](raw/2026-09-08-device-control-results/linked-frameworks.txt)에도 CoreAudio 프레임워크가 없습니다.
실제 기본 출력·샘플레이트·볼륨은 변경하지 않았습니다. 실기기 비동기 적용, 다른 앱과의 경쟁,
채널별 음량이 연동되는 장치, 청취·절전/재연결은 이번 호스트 검사로 입증하지 않습니다.

두 번째 채널 쓰기가 실패하면 첫 채널의 변경이 남을 수 있습니다. 여러 형식 쓰기도 원자적이지 않습니다.
실패를 정확히 보고하고 앱에서 다시 조회하며 자동 복구나 실제 적용 완료를 주장하지 않습니다.
두 채널이 모두 0이면 하드웨어 비율만으로 이전 pan을 복원할 수 없어 기존 읽기 규칙상 중앙을 반환합니다.
누수 감지는 이 플랫폼의 ASan에서 지원하지 않아 사용하지 않았습니다. UI 배치 파일은 변경하지 않았습니다.

다음은 ARC-04의 C 클라이언트 장치 접근 책임 분리입니다. 전체 지속 개발 목표는 활성 상태입니다.
