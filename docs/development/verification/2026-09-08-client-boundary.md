# 2026-09-08 클라이언트 장치 접근 책임 분리

기존 UI/UX와 공개 C API를 유지했습니다. `ClientShim.cpp`의 C 값 변환·엔진 상태/증폭 조율과
CoreAudio 장치 IO를 구분했습니다. 새 기능이나 IPC 경로는 추가하지 않았습니다.

## 구조와 이동

| 파일 | 역할과 범위 |
|---|---|
| `engine/client/ClientShim.cpp` | 869→432행. C 문자열/배열 변환, 엔진 명령·상태와 증폭 조율, 기존 `EngineConnection` 소유 |
| `engine/client/CoreAudioDevices.hpp` | 장치 ID·조회 결과·쓰기 요청의 내부 C++ 인터페이스. C ABI와 별개인 작은 결과 구조체 |
| `engine/client/CoreAudioDevices.cpp` | 458행. 장치 열거·속성·형식·볼륨·밸런스·기본 출력의 CoreAudio IO. Mach·엔진 상태·캐시·UI·저장 책임 없음 |
| `Package.swift`, `core/tests/CMakeLists.txt` | 앱 클라이언트와 두 C 검사 대상에 새 소스 연결 |
| `engine/tests/ClientDeviceFixture.hpp`, `test_client_devices.cpp` | 장치 목록·버퍼 제한·형식·연속 샘플레이트·지연 구성·읽기 무부작용 검사 보강 |

대규모 클래스를 다른 파일에 그대로 옮기는 방식이 아닙니다. 장치 모듈에는 엔진을 조회하는 호출이나
공유 연결이 없고 호출자가 장치 ID와 값을 전달합니다. 호출자는 HAL 속성 API를 직접 호출하지 않습니다.
함수와 결과 타입을 명시하는 헤더가 추가되므로 전체 행 수 감소를 성능·설계 품질의 증거로 사용하지 않습니다.
장치 열거·형식·볼륨이 같은 HAL 경계의 보조 함수를 공유하도록 두었으며 범용 플러그인 계층은 도입하지 않았습니다.

## 분리 전후 검증

장치 조회 검사를 먼저 추가하고 분리 전 코드에서 통과시켰습니다.
그때의 실행 파일을 보존한 뒤 동일 검사를 분리 후 코드로 다시 실행했습니다.
[분리 전](raw/2026-09-08-client-boundary/before.txt), [분리 후](raw/2026-09-08-client-boundary/after.txt).

추가 검사 범위:

- Roomcut 가상장치를 제외한 실장치 목록·C 문자열·잘린 버퍼의 종료 문자.
- 잘못된 인덱스·null 출력·없는 장치의 기존 오류와 출력 인자 보존.
- 물리 비트 깊이·명목 샘플레이트와 장치/스트림/안전 여유/버퍼 지연의 합산.
- 출력 버퍼 크기와 별개인 전체 형식 개수, 연속 샘플레이트 확장·중복 제거·순서.
- 기본 출력 읽기의 0·1·없는 장치 구분과 조회 과정의 장치 쓰기 0건.
- 기존 볼륨·기본 출력·형식·밸런스의 정상/실패 검사는 그대로 유지.

[경계 비교 결과](raw/2026-09-08-client-boundary/boundary-audit.txt):

- 이동한 HAL 보조 함수 18개의 본문이 식별자·공백 정규화 후 일치합니다.
- C 함수 19개와 엔진/정책 보조 함수 5개의 본문도 같은 방식으로 일치합니다.
- 결과 타입과 위임 형태가 바뀐 C 함수 5개는 실제 호출 검사를 별도로 실행했습니다.
- 공개 `roomcut_client.h`는 바이트 단위로 동일하고 C 내보내기 심볼 24개가 유지됐습니다.
- 객체 파일의 미해결 심볼을 확인했습니다. 장치 모듈은 Mach/클라이언트 함수에 의존하지 않고,
  C shim에는 직접 HAL/CF 호출이 없습니다.
- 보존한 분리 전 실행 파일과 분리 후 실행 파일의 종료값·표준 출력·표준 오류가 일치합니다.

[사용한 일회성 비교 스크립트](raw/2026-09-08-client-boundary/audit.py.txt)도 보관했습니다.
재실행에는 `build/client-boundary-audit`의 당시 소스/실행 파일이 필요합니다. 지속적인 동작 회귀는 CTest가 담당합니다.

이 비교는 모든 하드웨어·입력에서 수학적으로 동등함을 증명하는 것은 아닙니다.
정규화 비교와 실제 검사 범위를 함께 제시하며 테스트 출력만으로 전체 제품의 완성을 주장하지 않습니다.

첫 일회성 본문 비교 도구는 들여쓴 `roomcutClientGetState` 호출문을 함수 선언으로 잘못 찾아
`AssertionError: roomcutClientGetState`로 중단됐습니다. 행 시작의 함수 정의만 찾도록 비교 도구를 수정한 뒤
다시 비교했습니다. 제품 코드를 바꿔 이 비교를 통과시킨 것은 아닙니다.

## 전체 검사

| 검사·명령 | 결과 | 자료 |
|---|---|---|
| `cmake --build build -j 4` | 성공 | [네이티브 빌드](raw/2026-09-08-client-boundary/native-build.txt) |
| `ctest --test-dir build --output-on-failure` | 37/37 통과 | [전체 CTest](raw/2026-09-08-client-boundary/ctest.txt) |
| `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift test -c release --jobs 4` | 175/175 통과 | [전체 Swift](raw/2026-09-08-client-boundary/swift-tests.txt) |
| 별도 Debug 빌드의 ASan·UBSan | 장치/볼륨 2/2 통과 | [계측 검사](raw/2026-09-08-client-boundary/asan-test.txt) |
| `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer bash scripts/build-app.sh release` | 앱 조립 성공 | [앱 빌드](raw/2026-09-08-client-boundary/app-build.txt) |
| `codesign --verify --deep --strict build/Roomcut.app` | 종료값 0 | 로컬 서명 무결성 검사이며 공증·배포 검증 아님 |
| 최신 앱의 메모리 fixture | Home→Inspect→Home 전환·기존 표시 확인 | [UI 관찰](raw/2026-09-08-client-boundary/ui-smoke.txt) |

ASan·UBSan은 `build/client-boundary-audit/asan`에 `-fsanitize=address,undefined -fno-omit-frame-pointer`로 구성하고
`test_client_devices`, `test_client_volume`을 빌드했습니다.
`ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/client-boundary-audit/asan -R '^client_(volume|devices)$' -V`로 실행했습니다.
이 플랫폼의 ASan은 누수 감지를 지원하지 않으므로 누수 검증 통과를 주장하지 않습니다.

## 참고와 한계

[C++ Core Guidelines I.1·I.2](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Ri-explicit)를 2026-09-08 확인했습니다.
명시적인 인터페이스와 전역 상태를 통한 암묵적 의존성 지양을 참고했습니다.
기존 엔진 연결 캐시의 위치·수명·오류 재시도 계약은 바꾸지 않았습니다.

네이티브 장치 검사는 실제 C 함수/Mach와 모의 HAL이며 운영 장치에 쓰지 않습니다.
실기기 지연·경쟁·재연결·청취·성능 향상을 이번 분리로 입증하지 않습니다.
기존에 남아 있던 부분 적용과 비동기 HAL 완료의 한계도 유지합니다.
새 장치 모듈의 원문과 빌드 연결은 전체 구현 문서 생성기에 포함합니다.
다음 작업은 RT-01의 Room Tune 입력 신뢰도이며 전체 지속 개발 목표는 활성 상태입니다.
