# 공식 레퍼런스 목록

확인일: 2026-09-08. 아래 링크를 웹에서 직접 열었습니다. 외부 자료 원문 전체를 저장하지는 않고
근거와 Roomcut의 구현 선택을 구분합니다. 이후 관련 작업을 시작할 때 링크와 내용이 달라졌는지 다시 확인합니다.

| 출처 | 확인한 내용 | 적용 위치·판단 | 한계 |
|---|---|---|---|
| [Apple XNU Mach message header](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/man/mach_msg_header.html) | 수신 메시지의 크기와 헤더·본문·trailer 구분 | `roomcut_handshake.h`, `Heartbeat.cpp`, 드라이버 생존 확인의 필수 길이 검사 | 프로토콜의 유효 상태와 하위 호환 범위는 Roomcut이 정의 |
| [Apple XNU mach_msg](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/osfmk/man/mach_msg.html) | 시간 초과·권한 종류·실패 시 pseudo-receive | HELLO의 송수신 제한과 실제 권한 수 검사 | 전송 반환 성공만으로 취소한 상대가 응답을 소비했다고 판단하지 않음 |
| [Apple libsyscall mach_msg.c](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/libsyscall/mach/mach_msg.c) | 복구 가능한 전송 오류와 `mach_msg_destroy`가 별도로 정리하지 않는 local 권한 | `roomcut_mach_cleanup.h`; C/C++ HELLO·생존 확인·제어 요청의 시간 초과 정리 | 부분 소멸 가능한 다른 오류는 같은 방식으로 재해제하지 않음 |
| [C++ 작업 초안 numeric_limits](https://eel.is/c++draft/numeric.limits.members) | 서로 다른 부동소수점 값을 구별하는 십진수 자릿수 `max_digits10` | 엔진 DSP 숫자의 텍스트 저장 정밀도 | 실제 기존 파서로 소수·작은 값·인접 double 값의 왕복을 별도로 검사 |
| [Apple CoreAudio 알림 등록](https://developer.apple.com/documentation/coreaudio/audioobjectaddpropertylistenerblock(_:_:_:_:)) | 제공한 큐로 알림을 전달하고 block/queue를 등록 수명 동안 보유함. IO 문맥 예외도 명시 | `AudioPropertyListener`, `DeviceWatcher`, `VolumeController`; SDK 헤더와 공식 Markdown 본문 대조 | 제거가 이미 진행 중인 콜백의 즉시 소멸을 보장한다고 가정하지 않음 |
| [Apple CoreAudio 알림 해제](https://developer.apple.com/documentation/coreaudio/audioobjectremovepropertylistenerblock(_:_:_:_:)) | 등록한 object/address/queue/block 식별 정보로 해제 | 등록 성공만 추적하고 실패 시 재시도; 지연 콜백 비활성화 | 실제 HAL 읽기 전용 등록/해제와 모의 실패 검사를 구분 |
| [Apple AudioOutputUnitStart](https://developer.apple.com/documentation/audiotoolbox/audiooutputunitstart(_:)) | IO 유닛 처리 그래프의 시작은 별도 동작이며 결과 코드를 반환함 | 설정된 샘플레이트와 실제 시작 성공을 분리한 복구 검사 | 시작 실패는 실제 OutputDevice에 모의 HAL 실패를 주입해 검사 |
| [Apple TN2091](https://developer.apple.com/library/archive/technotes/tn2091/_index.html) | AUHAL과 실제 장치 형식·클라이언트 형식의 관계, 형식 변경의 중단 가능성 | 실제 유닛의 하드웨어 형식 비교와 불필요한 재개방 억제 | 오래된 문서의 예제 API를 복사하지 않음. 500ms/2초/5초 제한은 Roomcut의 설계 선택 |
| [Apple 앱 응답성 지침](https://developer.apple.com/documentation/xcode/improving-app-responsiveness) | UI 외 동기 작업을 메인 스레드에서 분리하고 단순 Task 생성만으로 해결됐다고 보지 않음 | Live 장치 읽기 전용 dispatch 큐·메인 액터 진행 검사. 공식 Markdown 본문 확인 | 전체 화면 지연·실기기 장시간 반응 속도를 대신하는 검사는 아님 |
| [Swift SE-0306](https://raw.githubusercontent.com/swiftlang/swift-evolution/main/proposals/0306-actors.md) | actor의 await 구간 재진입과 실행 순서 보장 범위 | 장치 명령의 단일 진행 상태·명시적 대기 순서·리비전 검사 | MainActor 지정만으로 복합 명령 전체가 원자적으로 실행된다고 가정하지 않음 |
| [Swift SE-0304](https://raw.githubusercontent.com/swiftlang/swift-evolution/main/proposals/0304-structured-concurrency.md) | async/await만으로 작업이 병렬화되는 것은 아님. 작업 수명·취소와 대기열 부담 구분 | 독립 장치 읽기·공유 대기·최신 요청 제한 | 이미 시작한 동기 HAL 호출이 논리적 취소와 함께 종료된다고 가정하지 않음 |
| [GNU Mach 메시지 전송](https://www.gnu.org/software/hurd/gnumach-doc/Message-Send.html) | 권한을 운반하는 메시지와 전송 오류 처리 | 포트 소유권 감사의 보조 자료 | GNU 설명만으로 Darwin 동작을 단정하지 않고 실제 macOS Mach 검사 진행 |
| [W3C Audio EQ Cookbook](https://webaudio.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html) | 정규화 biquad 계수와 필터 형식 | `Biquad.hpp`, 파라메트릭 응답 검사, 동적 EQ 후보 | 계수식만으로 실시간 파라미터 전환의 안정성을 보장하지 않음 |
| [FabFilter Pro-Q 4 Dynamic EQ](https://www.fabfilter.com/help/pro-q/using/dynamic-eq) | 밴드별 동적 범위·임계값·attack/release·추가 제어 | 기존 Advanced 편집 안의 선택적 동적 제어 후보 | 사용 흐름 참고. 화면·알고리즘·성능 동등성 주장 없음 |
| [REW Making Measurements](https://www.roomeqwizard.com/help/help_en-GB/html/makingmeasurements.html) | 측정 레벨, 시간 기준, 입출력 클록 차이 | Room Tune 정렬·입력 검증·재측정 조건 | iPhone 마이크·AGC와 현재 녹음 경로는 별도 실측 필요 |
| [Clang ThreadSanitizer](https://clang.llvm.org/docs/ThreadSanitizer.html) | 계측 기반 데이터 경합 검사와 실행 비용 | 메일박스·링·콜백 소유권·연결 캐시 검사 | 검사 통과가 모든 스케줄링의 안전성이나 성능을 보장하지 않음 |
| [Apple Creating custom audio effects](https://developer.apple.com/documentation/avfaudio/creating-custom-audio-effects) | 공식 페이지 존재 확인 | 이후 실시간 DSP 지침을 본문이 제공되는 경로에서 재확인 | 이번 웹 도구는 본문을 추출하지 못했으므로 세부 구현의 확인된 근거로 사용하지 않음 |
| [OpenAI 예약 작업](https://learn.chatgpt.com/docs/automations?surface=app) | 같은 작업의 일정 실행과 로컬 컴퓨터·앱 실행 조건 | 매시간 지속 개발 자동화 | 기기·사용 한도·도구 상태에 따라 연속 실행이 제한될 수 있음 |

이전 성능·A/B 개발 때는 ITU K 가중 필터, Apple 파라미터 램프, DSP Concepts 평활화 자료를 인용했고
[이전 진행 기록](../../ENGINEERING_PLAN.md)에 남겨 두었습니다. 이번 목록의 직접 확인 자료와 구분합니다.
해당 기능을 다시 수정할 때 원문과 판본을 확인하고 이 표에 새 확인일과 연결할 검사를 추가합니다.

## 갱신 규칙

2026-09-08 RT-04 확인: [Swift 동시성](https://docs.swift.org/swift-book/LanguageGuide/Concurrency.html)과
[Apple 액터 설명](https://developer.apple.com/videos/play/wwdc2021/10133/)의 await 사이에 상태를 다시 검증한다는 원칙을
직접 선택/임시 복구의 소유권에 적용했습니다. 지연된 실제 호출 순서와 UI 요청값은 TSan과 별도로 검사합니다.

2026-09-08 RT-03 확인: [AVCaptureSession.startRunning](https://developer.apple.com/documentation/avfoundation/avcapturesession/startrunning())와
[Apple의 세션 응답성 설명](https://developer.apple.com/videos/play/wwdc2026/303/),
[Swift Task.cancel](https://developer.apple.com/documentation/swift/task/cancel()),
[Swift 동시성 문서](https://docs.swift.org/swift-book/LanguageGuide/Concurrency.html)를 확인했습니다.
동기 세션 작업은 직렬 큐로 보내고 취소 뒤에도 이미 시작한 작업이 정리를 마칠 때까지 기다립니다.
Roomcut의 모의 전송/실제 큐 검사는 개별 AV 장치의 모든 콜백/중단 조건을 증명하지 않습니다.

2026-09-08 RT-02 확인: [CMSampleBufferCopyPCMDataIntoAudioBufferList](https://developer.apple.com/documentation/coremedia/cmsamplebuffercopypcmdataintoaudiobufferlist(_:at:framecount:into:))의
PCM 복사·채널 수/버퍼 크기 조건, [AVAudioConverter channelMap](https://developer.apple.com/documentation/avfaudio/avaudioconverter/channelmap)의
명시적인 입력 채널 선택, [TN3136](https://developer.apple.com/documentation/technotes/tn3136-avaudioconverter-performing-sample-rate-conversions)의
같은 샘플레이트 형식 변환과 SRC 호출 방식 차이를 확인했습니다. `RoomTunePCM`은 입력 샘플레이트를 그대로 두고 첫 채널을 보존합니다.
[REW 측정 지침](https://www.roomeqwizard.com/betahelp/help/html/makingmeasurements.html)과
[클록 조정 안내](https://www.roomeqwizard.com/betahelp/help/html/analysis.html)도 같이 읽었습니다.
Roomcut의 합성 클록 편차 검사가 REW의 추가 시간 기준 신호나 클록 보정을 구현했다는 뜻은 아닙니다.

2026-09-08 ARC-04 설계 참고: [C++ Core Guidelines I.1·I.2](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Ri-explicit)의
명시적인 인터페이스를 두고 암묵적인 전역 상태 전달은 피하라는 원칙을 확인했습니다.
`CoreAudioDevices`에는 장치 ID·값·조회 결과를 전달하며 엔진 연결이나 상태를 숨겨서 조회하지 않습니다.
지침 준수를 파일 행 수 감소만으로 판정하지 않습니다. 공개 C 변환과 기존 Mach 연결은 호출자에 남겼습니다.

2026-09-08 추가 확인: [Apple AudioObjectSetPropertyData](https://developer.apple.com/documentation/coreaudio/audioobjectsetpropertydata(_:_:_:_:_:_:))는
OSStatus로 쓰기의 성공/실패를 보고하며, 실제 속성 반영은 HAL 알림 전까지 완료됐다고 가정하지 말라고 명시합니다.
[AudioObjectIsPropertySettable](https://developer.apple.com/documentation/coreaudio/audioobjectispropertysettable(_:_:_:))의
질의 실패와 쓰기 비지원도 구분합니다. IO-01의 C 볼륨 쓰기에 적용했습니다.
반환값 0은 필요한 쓰기 요청의 수락이며 여러 장치의 원자 적용·청취 결과를 뜻하지 않습니다.
같은 계약을 IO-02의 기본 출력·샘플레이트·밸런스 쓰기에도 적용했습니다.
Apple의 [기본 출력 속성](https://developer.apple.com/documentation/coreaudio/kaudiohardwarepropertydefaultoutputdevice)과
[장치 명목 샘플레이트](https://developer.apple.com/documentation/coreaudio/kaudiodevicepropertynominalsamplerate)도 확인했습니다.
웹 도구가 쓰기 API의 Markdown을 처리하지 못해 공식 페이지가 제공한 `.md` 주소를 직접 읽었습니다.
C 함수의 이미 적용됨 값 `1`은 Roomcut 자체 계약이며 Apple의 OSStatus와 혼동하지 않습니다.

자료마다 URL, 확인일, 실제 읽은 내용, 적용할 파일/이슈, 미확인 부분을 기록합니다.
블로그·검색 요약보다 공식 SDK 문서·공개 소스·표준·제품 제작자의 설명을 우선합니다.
자료가 바뀌어도 기존 UI를 임의 재설계하지 않습니다. 새 자료만 모으지 말고 검증 가능한 구현/실험으로 연결합니다.
