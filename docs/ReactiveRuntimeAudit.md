# iiSDK 이벤트·쿼리 반응형 런타임 감사

## 결정과 판정 기준

2026-11-06 사용자는 iiSDK 전체의 공통 개념을 **이벤트와 쿼리로 반응하는 네이티브 런타임**으로 결정했다. 변경 생산자는 영향을 받은 자원·판본·필드를 통지하고, 소비자는 필요한 범위를 쿼리하여 갱신한다. 유휴 상태의 주기적 상태 조회, 변경 없는 전체 갱신, 완료 여부 확인을 위한 암묵적 반복을 기본 정책으로 두지 않는다.

`poll()`이라는 이름이나 `while` 문만으로 위반을 판정하지 않는다. OS 소켓을 기한까지 **블로킹하여 준비 이벤트를 기다리는 `poll`**, 호출자가 명시한 단발 조회나 폴링, 실제 만료·취소·재시도 기한, 프로토콜 생존 확인, 움직이는 화면과 게임의 프레임, 이벤트 병합용 단발 타이머는 성격을 구분한다. 외부 서비스가 완료 알림을 제공하지 않는 경우도 지원 기능과 명시적 대체 정책을 확인해야 한다. 이 문서는 소스 분석이며 CPU 사용률이나 장치 실행 결과를 측정하지 않았다.

SDK 간 독립성도 유지한다. Core의 타입을 모든 SDK가 직접 링크하는 구조가 아니라, 각 모듈이 표준 C++ 값·콜백·수명 계약을 제공하고 앱 또는 별도 조합 계층이 실행기와 전달 경로를 연결한다. 기능 변경을 수반하는 이전에는 기존 권한·지속성·복구·접근성 계약을 보존해야 한다.

## 검사 범위와 한계

54개 SDK 디렉터리와 SDK 공통 도구의 소스 3,569개, Markdown 1,497개를 정적 검색했다. `iiSocialPublish`는 `iiSocialIntegration`의 심볼릭 링크이므로 별도 소스로 중복 계산하지 않았다. 8개 디렉터리에서는 선택한 소스 파일이 없었다. 빌드 출력, 테스트, 예제, 리소스, 모델과 외부 원본은 제외했다. CMakeLists.txt의 전체 의존 그래프와 전처리 조합은 별도로 전수 분석하지 않았다.

[구조화 후보 목록](ReactiveRuntimeAudit.candidates.json)은 파일별 SHA-256, 검색 범위, 소스 후보 755개, 문서 후보 665개와 위치를 기록한다. 읽기 실패와 읽는 동안의 파일 변경은 없었다. 스캔 후 다른 작업에서 `LVUI/src/Compiler/Compiler.cpp`가 변경된 사실을 감지했으며 이 파일의 목록은 스캔 시점에 한정된다. 후보는 **어휘 일치 목록이지 1,420개의 확정된 위반 목록이 아니다**. 플랫폼 조건, 동적 연결, 검색어에 나타나지 않는 실행 경로, 프로세스 간 동작은 이 검색만으로 확정할 수 없다. 전체 워크스페이스를 동시에 동결한 원자적 스냅샷도 아니다. 아래 항목은 관련 호출과 수명 조건을 추가로 읽어 확인한 사례와 명시적인 계약 공백이다.

## 확인된 구현과 명세의 차이

| 우선순위 | 영역 | 확인한 동작과 방향 |
| --- | --- | --- |
| 높음 | iiWebServer | [NetworkTransport.cpp](../iiWebServer/src/Transport/NetworkTransport.cpp#L17)의 각 전송 런타임은 생성 시 2ms 반복 타이머로 Core 루프를 구동한다. [SessionAuthenticator.cpp](../iiWebServer/src/Peers/SessionAuthenticator.cpp#L16)도 요청 동안 2ms마다 구동한다. iiNetwork가 준비 이벤트를 제공해도 상위 계층에서 유휴 깨움이 다시 생긴다. 호스트 깨움과 실제 다음 기한을 연결해야 한다. |
| 보통 | iiWebServer 피어 기한 | [Peer.cpp](../iiWebServer/src/Peers/Peer.cpp#L34), [LanPeer.cpp](../iiWebServer/src/Peers/LanPeer.cpp#L110), [RelayServer.cpp](../iiWebServer/src/Peers/RelayServer.cpp#L34)는 실행 중 각각 100ms, 250ms, 500ms 간격으로 만료·요청 상태를 훑는다. 가장 가까운 실제 만료 기한과 연결 상태 변화에 맞춰 예약할 수 있는지 검토해야 한다. 인증 갱신과 연결 재시도 자체는 별도의 프로토콜 정책이다. |
| 높음 | iiNetwork 실패 통지 | 앞선 이벤트 전환에서 추가된 [SocketReactor.cpp](../iiNetwork/src/Transport/SocketReactor.cpp#L156)의 워커는 예외 시 등록을 지우고 끝나지만 대기 중 소비자에게 오류를 알리지 않으며 새 등록을 막는 실패 상태도 설정하지 않는다. 할당 실패 등 예외 조건에서 이벤트가 영구히 전달되지 않을 수 있다. 정상 경로의 유휴 타이머 제거와 별개로 실패 전파·후속 등록 거부를 보장해야 한다. |
| 높음 | iiGUI 데스크톱 | [HostWindows.cpp](../iiGUI/src/Platform/HostWindows.cpp#L239), [HostLinux.cpp](../iiGUI/src/Platform/HostLinux.cpp#L205)는 한 번 시작한 16ms cadence를 닫을 때까지 유지한다. 보조 창 열기도 이를 시작한다. 명시적으로 지속 프레임을 요청한 앱과 활성 모션은 유지하되, 일반 위젯의 마지막 모션이 끝나면 cadence를 중지해야 한다. [문서의 상시 타이머 없음](../iiGUI/README.md#L182)과 현재 경로가 일치하지 않는다. |
| 높음 | iiGUI iOS·Linux | [HostIOS.mm](../iiGUI/src/Platform/HostIOS.mm#L273)은 활성 상태에서 창을 열 때 display link를 시작하고, 모션 종료 여부와 관계없이 유지한다. Linux의 [AtSpiHost.h](../iiGUI/src/Widgets/AtSpiHost.h#L21)는 접근성 세션 동안 16ms마다 펌프하고, [NativeDialogsLinux.cpp](../iiGUI/src/Platform/NativeDialogsLinux.cpp#L31)는 포털과 메뉴의 GLib 큐를 10–16ms마다 확인한다. 디스플레이·버스의 네이티브 준비 및 구독 수명으로 전환해야 한다. |
| 보통 | iiGUI Linux 클립보드 | [DesktopLinux.cpp](../iiGUI/src/Platform/DesktopLinux.cpp#L26)의 X11 선택 응답 대기는 소켓 준비 이벤트를 기다리지만 고정 20ms마다 기한도 재확인하며 호스트 스레드를 동기적으로 점유한다. 남은 실제 기한으로 준비 대기를 걸거나 비동기 선택 이벤트로 연결할 수 있다. 클립보드 크기 제한과 시간 초과 보장은 유지해야 한다. |
| 높음 | iiSocietyHelper 동기화 | [Controller.cpp](../iiSocietyHelper/src/Sync/Controller.cpp#L175)는 파일 감시와 별도로 1초마다 동기화 `tick`을 구동한다. [indexHost](../iiSocietyHelper/src/Sync/Controller.cpp#L108)는 변경이 없어도 30초가 지나면 다시 `scan`한다. [명세](../iiSocietyHelper/docs/Sync/README.md#L95)는 감시 한도와 누락 사건을 이 방식으로 보완한다고 명시한다. 알림 누락·감시 상한에 대한 복구 대책 없이 타이머만 지워서는 안 된다. |
| 높음 | Society 컨테이너 데몬 | [container-daemon.cpp](../iiSocietyHelper/tools/container-daemon.cpp#L141)는 기본적으로 5초마다 `Publisher::poll()`을 호출하며, [Monitor.cpp](../iiSocietyHelper/src/Monitoring/Monitor.cpp#L448)는 이때 각 영역의 파일 트리를 순회한다. [명세](../iiSocietyHelper/docs/Monitoring/Protocol.md#L46)에도 기본 전체 스캔이 명시되어 있다. 별도의 1초 heartbeat는 실제 생존 정책이며, 20ms 종료 확인은 신호 깨움으로 대체할 수 있다. 인덱스 완전성·감시 누락 복구 없이 전체 스캔을 제거해서는 안 된다. |
| 높음 | iiSocietyHelper 상태 구독 | [Monitor.cpp](../iiSocietyHelper/src/Monitoring/Monitor.cpp#L605)의 Subscription은 기본 250ms 주기로 스냅샷을 읽고 결과가 같을 때만 콜백을 억제한다. [프로토콜 명세](../iiSocietyHelper/docs/Monitoring/Protocol.md#L110)에도 이 동작이 있다. 데이터 파일 변경·생산자 종료 알림과 별도의 실제 생존 만료 기한이 필요하다. |
| 높음 | iiSocietyContainer 목록·요청 | [StorageDirectoryModel.cpp](../iiSocietyContainer/StorageDirectoryModel.cpp#L265)는 10초 전체 새로고침과 다운로드 요청 중 500ms 상태 조회를 사용하며, [StorageModelCatalog.cpp](../iiSocietyContainer/StorageModelCatalog.cpp#L286)는 감시와 별도로 30초 갱신한다. [DashboardFiles.cpp](../iiSocietyContainer/src/Gui/DashboardFiles.cpp#L176)도 10초 대체 갱신을 시작한다. 변경 통지에는 자원 ID·판본·변경 범위와 누락 복구 경계가 필요하다. |
| 높음 | iiLocalLLM | [LocalIpcServer.cpp](../iiLocalLLM/src/service/LocalIpcServer.cpp#L93)는 연결이 없어도 10ms마다 클라이언트와 완료 대기 목록을 확인한다. [Scheduler.cpp](../iiLocalLLM/src/service/Scheduler.cpp#L37)는 대기열이 비었을 때도 100ms마다 maintenance를 호출하고, [HttpApiServer.cpp](../iiLocalLLM/src/service/HttpApiServer.cpp#L370)는 스트림 완료를 10ms 간격으로 확인한다. 완료·출력·연결 종료 통지와 실제 maintenance 기한을 각각 분리해야 한다. |
| 높음 | iiMultimodalAgent MCP | [MCP/Server.cpp](../iiMultimodalAgent/src/MCP/Server.cpp#L198)는 초기화·요청·페이지 만료를 10ms마다 전수 확인하고, [StdioServer.cpp](../iiMultimodalAgent/src/MCP/StdioServer.cpp#L54)는 입력이 없어도 10ms 후 다시 깨어난다. [McpConnections.h](../iiMultimodalAgent/src/Agent/Integrations/McpConnections.h#L18)의 기본 갱신 간격은 250ms이다. 가장 가까운 실제 기한, 입출력 준비 및 구성 변경 알림을 각각 다뤄야 한다. |
| 보통 | iiMultimodalAgent 전송·취소 | [StdioClientTransport.cpp](../iiMultimodalAgent/src/MCP/StdioClientTransport.cpp#L45)는 연결된 하위 프로세스의 출력을 10ms 간격으로 기다린다. [GraphQL.cpp](../iiMultimodalAgent/src/Agent/Integrations/GraphQL.cpp#L33), [WebFetch.cpp](../iiMultimodalAgent/src/Agent/Integrations/WebFetch.cpp#L58), [HttpHook.cpp](../iiMultimodalAgent/src/Agent/Hooks/HttpHook.cpp#L78)는 취소 확인에 10ms 타이머를 사용한다. [MultimodalTools.cpp](../iiMultimodalAgent/src/Agent/Tools/MultimodalTools.cpp#L147)는 생성·음성 취소를 별도 스레드에서 5ms마다 중계한다. 준비·완료·취소 통지를 연결해야 하며 중단된 외부 요청의 결과 불확실성은 보존해야 한다. |
| 높음 | iiMultimodalAgent 사용자 질문·작업 | [QuestionInbox.cpp](../iiMultimodalAgent/src/Agent/Collaboration/QuestionInbox.cpp#L24)는 100ms마다 대기 질문 전체를 페이지로 다시 읽고 비교한다. [PermissionRequests.cpp](../iiMultimodalAgent/src/Agent/Permissions/PermissionRequests.cpp#L99), [Runtime/Api.cpp](../iiMultimodalAgent/src/Agent/Runtime/Api.cpp#L237), [Subagents.cpp](../iiMultimodalAgent/src/Agent/Collaboration/Subagents.cpp#L361), [Engine.cpp](../iiMultimodalAgent/src/Agent/Runtime/Engine.cpp#L2170) 등에는 응답·취소·future 완료를 5–20ms 간격으로 기다리는 경로가 있다. 취소 가능한 완료 통지와 항목별 변경이 필요하다. |
| 보통 | iiMultimodalAgent LSP·협업 | [LspProtocol.cpp](../iiMultimodalAgent/src/Agent/Integrations/LspProtocol.cpp#L197)는 대기열이 비어도 10ms마다 깨어나 언어 서버를 확인한다. [Teams.cpp](../iiMultimodalAgent/src/Agent/Collaboration/Teams.cpp#L351)는 구성원별 500ms 대기 후 메시지와 작업을 살피고 작업 완료도 10ms마다 확인한다. 입출력 준비, 받은 메시지, 완료 및 취소를 개별 사건으로 연결해야 한다. |
| 높음 | LVUI 비동기 작업 | [NativeImageAsync.h](../LVUI/src/Media/NativeImageAsync.h#L64)는 이미지 작업이 남은 동안 8ms마다 완료 목록을 훑는다. [NativeWork.cpp](../LVUI/src/Runtime/NativeWork.cpp#L26)는 수명 종료 대기 중 기본 1ms 간격으로 `try_close`를 재시도한다. 예약 슬롯 포화 시 완료를 잃지 않는 기존 보증과 UI 수신자 퇴역 장벽을 유지하면서 완료 신호로 전환해야 한다. |
| 높음 | iiAccountManager | [SocietySession.cpp](../iiAccountManager/Native/src/Session/SocietySession.cpp#L32)는 연결 중 30초, 연결 실패 시 3초마다 라이선스를 다시 요청하고 데몬 기동 시에는 100ms 재접속을 반복한다. 만료 시각 확인은 실제 기한이지만 성공 후 반복 검증과 실패 복구는 별도의 정책이다. 앱 시작 시 확인, 데몬 상태·권한 변경 통지, 만료와 오프라인 보증을 분리해야 한다. |
| 보통 | iiAccountManager 로그아웃 완료 | [BrowserSession.cpp](../iiAccountManager/Native/src/Session/BrowserSession.cpp#L580)는 저장소 삭제 완료를 런타임에 전달할 때 큐가 가득 찬 동안 2ms마다 재접수한다. 완료 유실을 막는 목적을 보존하면서 수신 용량 확보 통지나 예약 완료 슬롯을 사용해야 한다. |
| 보통 | iiAccountManager·Society | [AccountManager.cpp](../iiAccountManager/src/Accounts/AccountManager.cpp#L245)의 컨테이너 위치 동기화는 활성화 시 30초 조회이며 [AccountSession.cpp](../iiSocietyHelper/src/Account/AccountSession.cpp#L31)가 이를 켠다. 이는 명시적 활성화가 있는 기능이지만, 서버 푸시와 구독 수명으로 대체할 수 있는지 별도 확인해야 한다. |
| 보통 | iiSocietyHelper 페어링·생성 | [AutomaticPairing.cpp](../iiSocietyHelper/src/Discovery/AutomaticPairing.cpp#L5)는 변경 시그널을 받으면서도 생성 직후 500ms 반복을 시작한다. [Client.cpp](../iiSocietyHelper/src/Client/Client.cpp#L96)는 활성 연결을 1초마다 점검하고, [Remote.cpp](../iiSocietyHelper/src/Generation/Remote.cpp#L5)는 진행 중 작업의 상태를 250ms마다 확인한다. 발견·상태 변경 알림과 재시도·만료의 실제 기한을 분리해야 한다. |
| 보통 | iiSocietyHelper 파일 동기화 | 기본 탐색 모드의 [FileSystem.cpp](../iiSocietyHelper/src/Files/FileSystem.cpp#L279)는 컨테이너의 `refreshRequested`를 구독하면서도 2초마다 동기화를 요청한다. 감시 실패와 외부 변경의 복구 경로를 확인한 뒤 통지와 명시적 재조정을 구분해야 한다. [Helper.cpp](../iiSocietyHelper/src/Core/Helper.cpp#L542)의 heartbeat와 감시 불가 시 폴링 대체는 생존·복구 정책을 별도로 보존해야 한다. |
| 보통 | 저장 경합·취소 | [FileRepository.cpp](../iiSocietyContainer/FileRepository.cpp#L51)는 전경 작업 우선권을 20ms마다 확인하고 저장 경합도 10–25ms 재시도한다. [Database.cpp](../iiFileProvider/Native/src/Database/Database.cpp#L378), [ObjectProvider.cpp](../iiSocietyHelper/src/Sync/ObjectProvider.cpp#L92)에도 짧은 간격 확인이 있다. 프로세스 간 잠금·우선권·취소의 정확성을 보존할 수 있는 대기·통지 수단이 필요하다. |
| 보통 | iiFileProvider 패키징 취소 | [ObjectPackage.cpp](../iiFileProvider/tools/ObjectPackage.cpp#L30)의 보조 스레드는 패키징 도구가 실행되는 동안 프로세스 종료 신호를 50ms마다 확인한다. OS 신호를 안전한 깨움 수단으로 전달하여 취소를 요청할 수 있다. 반면 [ObjectPackager.cpp](../iiFileProvider/src/Objects/ObjectPackager.cpp#L66)의 반복은 예약된 입력을 처리하는 작업 루프이므로 유휴 폴링이 아니다. |
| 보통 | iiPhotoLibrary | [PhotoController.cpp](../iiPhotoLibrary/src/Library/PhotoController.cpp#L51)는 저장소를 연 뒤 30초마다 `refresh()`한다. [갤러리 모델](../iiPhotoLibrary/src/Library/PhotoController.cpp#L19)은 순서나 행 수가 조금만 달라져도 전체 모델을 초기화하며, 스냅샷은 각 미리보기의 파일 상태를 다시 확인한다. [PhotoLibraryApple.mm](../iiPhotoLibrary/src/Platform/PhotoLibraryApple.mm#L88)은 네이티브 완료를 기다리는 동안 250ms마다 취소를 확인한다. 갤러리의 16ms 단발 뷰포트 병합은 반복 폴링과 다르다. 변경 범위와 취소 통지, 누락 복구를 정의해야 한다. |
| 보통 | iiDeviceMultimedia | [Hardware.h](../iiDeviceMultimedia/src/IO/Hardware.h#L69)는 네이티브 콜백을 큐에 넣지만 호스트가 `poll()`해야만 수집·전달한다. [조합 예제](../iiDeviceMultimedia/docs/independence.md#L45)는 앱이 명시적으로 2ms 타이머를 선택하므로 기존 폴링 예외에는 해당한다. 다만 타이머 없이 새 데이터가 있음을 호스트에 알릴 독립 깨움 계약이 부족하다. [Apple MIDI 루프](../iiDeviceMultimedia/src/IO/HardwareApple.mm#L457)는 별도로 25ms마다 깨어난다. |
| 보통 | iiUpdateManager·iiAudioGen | [NativeInstaller.cpp](../iiUpdateManager/Native/src/Installer/NativeInstaller.cpp#L210)는 검증 프로세스의 출력·종료를 5ms마다 확인한다. [ChildProcess.cpp](../iiAudioGen/src/Process/ChildProcess.cpp#L155)는 POSIX 종료 시 10ms 반복, Windows 파이프에서 2ms 대기를 사용한다. 모듈 독립성을 유지하는 교체 가능한 프로세스 완료·파이프 준비 계약이 필요하다. |
| 보통 | iiSharedCanvas 미디어 변환 | [MediaIo.cpp](../iiSharedCanvas/src/Media/MediaIo.cpp#L242)는 변환 프로세스의 취소·기한·파이프를 10ms 대기 반복으로 확인한다. 데이터 준비와 종료 통지, 취소 및 실제 기한을 분리하면서 입력 역압과 진단 출력 한도도 유지해야 한다. 반면 [AsyncFrameRenderQueue](../iiSharedCanvas/src/Render/AsyncFrameRenderQueue.cpp#L40)는 작업 완료 시 깨움 콜백을 보내므로 레이어 처리 반복 자체를 폴링으로 분류하지 않는다. |
| 보통 | iiGeneralDocument 변환 | [ConversionProcess.cpp](../iiGeneralDocument/src/Word/Private/ConversionProcess.cpp#L255)의 POSIX 변환기는 파이프를 확인하면서 자식 종료를 `waitpid(WNOHANG)`으로 조회하고 최대 20ms마다 재검사한다. 파이프 준비, 자식 종료, 실제 변환 기한을 함께 기다리는 플랫폼별 방식이 필요하다. 종료 시 자식 프로세스 그룹 정리와 출력 크기 제한은 보존해야 한다. |
| 보통 | iiVirtualizer | 게스트 [Init.cpp](../iiVirtualizer/src/guest/Init.cpp#L148)는 20ms마다 포트 탐색, 입출력, 작업 완료와 자식 상태를 함께 훑는다. [ProcessSupervisor.cpp](../iiVirtualizer/src/guest/ProcessSupervisor.cpp#L54)와 [Runtime.cpp](../iiVirtualizer/src/Runtime/Runtime.cpp#L54)에도 자식·VM 상태 재조회가 있다. virtio 준비, 자식 종료, 취소와 실제 시간 제한을 분리해야 한다. |
| 보통 | iiLocalDiffusion | [NativeDiffusion.cpp](../iiLocalDiffusion/src/Generation/NativeDiffusion.cpp#L850)는 pose 전처리 중 취소·기한을 10ms마다 확인하고, [NativeExecutionControl.hpp](../iiLocalDiffusion/src/Generation/NativeExecutionControl.hpp#L24)는 일시정지 중 취소를 20ms마다 확인한다. 중단 통지와 pause-adjusted 기한을 보존하는 대기 계약이 필요하다. 명시적으로 켠 [진단 heartbeat](../iiLocalDiffusion/src/Generation/NativeTelemetry.hpp#L110)는 별도로 취급한다. |
| 보통 | iiLocalDiffusion 참고 도구 | [comfyui_runtime.py](../iiLocalDiffusion/reference/diffusers/comfyui_runtime.py#L223)는 별도의 ComfyUI 작업 완료를 HTTP 이력 조회로 반복 확인한다. [문서](../iiLocalDiffusion/docs/comfyui-generation.md#L31)는 기본 1초 간격을 지정한다. 네이티브 SDK 경로가 아닌 참고 도구이며, 외부 런타임의 완료 이벤트 지원을 검증하고 명시적으로 선택한 제한된 대체 방식과 구분해야 한다. |
| 보통 | LVRS 런타임 계측 | [runtimeevents.cpp](../LVRS/src/backend/runtime/runtimeevents.cpp#L571)는 시작 시 idle 250ms와 OS 1초 타이머를 함께 시작한다. idle 임계치 도달은 실제 단발 기한으로 표현할 수 있다. RSS·uptime의 연속 표시와 heartbeat는 구독·표시 정책을 확인해야 하며, 필요 없는 소비자에게까지 계측을 강제하지 않아야 한다. |
| 보통 | LVRS 텍스트 모델 | [textdocumentmodel.cpp](../LVRS/src/backend/text/textdocumentmodel.cpp#L533)의 `replaceRange`는 일부 글자만 바뀌어도 변경 전후의 모든 행을 새 목록으로 복사하고 `beginResetModel()`로 전체 모델을 초기화한다. 한 글자 삽입도 [문자 수 계산](../LVRS/src/backend/text/textdocumentmodel.cpp#L1056)에서 전체 행을 다시 순회한다. 전체 문서 교체·초기 로딩의 초기화와는 별개로, 범위 편집은 영향을 받은 행의 삽입·삭제·변경을 통지하고 길이는 차분으로 갱신할 수 있다. 커서와 문자열 경계 계약은 유지해야 한다. |

### 반응형 계약의 공백

- [iiJob Store](../iiJob/src/Job.h#L59)는 영속 생성·판독·변경·재시도 API를 제공하지만 공개 변경 구독·판본 전달 계약은 보이지 않는다. 현재 API가 잘못된 저장을 한다는 뜻은 아니다. 별도 프로세스에서 진행 상태를 반응형으로 관찰하려면 성공한 트랜잭션 이후의 작업 ID·항목 ID·시퀀스와 재연결 복구 계약이 필요하다.
- [iiDeviceMultimedia의 명시적 `poll()`](../iiDeviceMultimedia/src/IO/Hardware.h#L69), [iiGPU의 명시적 `create_polling`](../iiGPU/src/Runtime/Completion.cpp#L31), [iiSDKBuilder의 Windows 파이프 정책](../iiSDKBuilder/src/Build/ChildIOPolicy.h#L8)은 호출 측이 선택한 폴링 경로이다. 이를 무조건 금지하지 않는다. 다만 이벤트 기반 경로가 없는 대상에서는 지원 범위 또는 명시적 대체 선택이 드러나야 한다.
- [Core 임베디드 계약](../iiSDKCore/docs/embedded.md#L7)은 보드 지원 계층이 `poll`을 구동하는 협력형 루프이다. 명시적 호스트 선택이므로 폴링 예외에 해당하지만, 인터럽트와 실제 다음 기한으로 잠들 수 있는 보드에서 비폴링 호스트 연결은 별도 기능으로 다뤄야 한다. 모든 보드가 그러한 대기 수단을 제공한다고 가정하지 않는다.
- [iiSocialIntegration](../iiSocialIntegration/src/Publishing/Publishing.cpp#L159)은 일부 원격 미디어 처리 상태를 설정된 주기로 조회한다. 외부 공급자가 푸시·웹훅을 제공하는지와 보안·중복 게시 방지 계약을 먼저 확인해야 한다. 푸시가 없는 공급자는 한정된 명시적 조회 정책이 필요하다.
- [SDK 독립성 지침](../AGENT.md#L5)은 모듈 간 직접 참조를 금지하지만 기존 [iiNetwork CMake](../iiNetwork/CMakeLists.txt#L11), [iiSDKBuilder CMake](../iiSDKBuilder/CMakeLists.txt#L15) 등은 이미 Core와 다른 SDK를 직접 참조한다. 반응형 전환이 이 직접 의존성을 더 늘리지 않도록 앱 조합 경계를 먼저 정의해야 한다. 이 독립성 전환도 아직 완료되지 않았다.

## 이미 확인된 기반과 오판 방지

[Core QueryPort](../iiSDKCore/src/Runtime/Query.h#L25)는 타입이 있는 동기 쿼리와 수명에 묶인 비동기 전달을 제공한다. [QueryStream](../iiSDKCore/docs/query-stream.md#L68)은 변경 영향을 받은 구독만 투영할 수 있으나, 임의 쿼리의 자동 색인을 제공하지 않는다. [Session](../iiSDKCore/src/Runtime/Session.h#L12)과 ObjectOwner는 소유 객체·작업의 종료 경계를 제공한다. 이는 모든 SDK가 이미 이를 채택했다는 뜻이 아니다.

[iiNetwork 소켓 감시](../iiNetwork/src/Transport/SocketReactor.cpp#L116)와 [iiGlobalSearch 대기](../iiGlobalSearch/src/Web/WebWait.h#L15)는 현재 소스에서 유휴 중 짧은 주기로 반복 확인하는 방식이 아니다. [iiGPU CompletionService](../iiGPU/src/Runtime/CompletionService.cpp#L48)는 네이티브 완료 대기와 통지를 제공하고, 별도의 명시적 폴링 API를 구분한다. [macOS GUI 호스트](../iiGUI/src/Platform/Host.mm#L150)는 외부 프레임 요청이나 활성 모션에 따라 display link를 시작·중지한다. 이 관찰을 다른 운영체제의 실행 검증으로 확대하지 않는다.

[Core Process의 기한 대기](../iiSDKCore/src/Process/Process.cpp#L307)는 지원되는 플랫폼에서 실제 종료 신호와 남은 기한을 기다리고, 신호가 없으면 자동 폴링으로 전환하는 대신 실패를 반환한다. [HostLoop 계약](../iiSDKCore/docs/host-loop.md#L14)도 호스트가 다음 기한에 단발 타이머를 연결하도록 요구한다. 이름에 `poll`이 등장하더라도 이와 같은 이벤트 루프의 준비 작업 호출과 고정 간격 재검사는 구분한다.

## 개선 순서와 유지해야 할 보장

1. 먼저 독립적인 준비·완료·변경 구독과 취소/퇴역 계약을 정하고 iiNetwork 감시 워커의 실패 전파와 후속 등록 거부를 바로잡는다. 이벤트는 자원·범위·판본을 나타내고 쿼리는 필요한 데이터만 반환한다. 앱이 모듈 간 연결을 소유한다.
2. iiWebServer 루프 연결, iiGUI의 유휴 프레임·Linux 버스, LVUI 작업 완료와 iiLocalLLM/에이전트 작업 완료를 전환한다. 반복 확인과 전체 목록 재구성의 발생 빈도를 먼저 측정한다.
3. Society 저장·동기화에는 커밋 뒤 변경 통지와 지속 판본을 도입하고 감시 한도 초과, 이벤트 유실, 재시작, 복제본 재연결의 복구 경로를 마련한 뒤 기존 전체 재검사 정책을 변경한다. 원본·권한·작업 체크포인트의 안전성을 우선한다.
4. 데몬·기기·외부 서비스처럼 통지가 제한된 경계는 플랫폼별 능력을 드러낸다. 만료·heartbeat·재시도·프레임, 사용자가 선택한 폴링은 명시적으로 구분한다.
5. 구독 등록과 최초 스냅샷 사이의 경쟁, 순서·중복·버퍼 초과, 늦은 콜백, 취소와 객체 파괴를 검증한다. 이벤트 누락 시에는 침묵하지 말고 재동기화 필요 상태를 노출한다. 세션 수명과 제품 프로세스 재시작 후의 영속 기록은 별도로 다룬다.

이 보고서 작성 중 소스 구현은 변경하지 않았으며 빌드, 앱·브라우저·기기 실행, 실제 성능 측정은 하지 않았다. 문서 원문과 에이전트 JSON의 해시·복원·링크는 검사했다. WorkspaceDocumentation의 SDK 전체 `check`는 기존 프로젝트 레지스트리가 `iiJob`을 자식 목록에서 제거하려 하여 통과하지 못했다. 실제 모듈 링크를 보존하기 위해 이 보고서 작업에서 해당 목록을 자동 변경하지 않았다. 위 항목 중 어느 것도 수정 완료로 간주하지 않는다.

## 2026-11-06 후속 구현과 검증 상태

위 표는 감사 당시의 스냅샷이다. 아래 항목은 그 후 해당 경로에 적용한 변경이며,
같은 행에 열거된 다른 플랫폼·타이머까지 해결되었다는 뜻은 아니다.

- iiNetwork 소켓 감시 워커의 실패는 등록된 TCP·UDP·일반/TLS 리스너에 오류를
  통지하고 후속 등록을 거부한다. 정상 준비와 취소된 감시, 주입된 장애와 중복 통지
  방지를 검사했다. 실패 주입은 테스트 빌드에서만 사용한다.
- iiWebServer 전송 루프와 세션 인증기의 2ms 타이머는 준비 신호 및 실제 다음 기한을
  사용하는 단발 디스패치로 전환했다. 피어의 100/250/500ms 만료·생존 경로는 남았다.
- iiGUI iOS는 활성 모션이나 명시적 프레임 루프가 있을 때만 display link를 유지한다.
  Linux X11 선택 응답은 기존 2초 기한을 지키면서 소켓 준비까지 기다린다. Windows·Linux
  창의 프레임 cadence와 Linux 접근성·포털 반복 구동은 남았다. iOS는 크로스 컴파일,
  X11 대기는 POSIX 준비 단위 검사로 확인했으며 장치 실행은 확인하지 않았다.
- iiSocietyHelper 자동 페어링은 발견·연결 변경과 실제 재시도·연결 만료 시각을 사용한다.
  동기화 감시·데몬·상태 구독·파일 시스템의 재검사는 이 변경에 포함되지 않는다.
- LVUI 이미지 작업은 예약된 완료 신호를 사용하며, 취소된 실행 작업이 신호 슬롯 퇴역
  후에도 새 슬롯에 다시 연결된다. `NativeWork`의 수명 종료 재시도는 남았다.
- iiMultimodalAgent 질문함은 요청 ID별 변경 통지와 단일 요청 조회를 사용한다. 초기
  구독과 명시적 `refresh()`는 전체 스냅샷으로 재조정한다. 권한 요청의 취소 대기,
  MCP·전송·협업의 다른 반복 확인은 남았다.
- iiPhotoLibrary 갤러리는 안정된 ID의 행 삽입·삭제·정렬을 부분 통지한다. 식별자가
  없거나 중복되면 안전하게 전체 재설정한다. 저장소 재조회와 Apple 취소 확인은 남았다.
- LVRS 텍스트 범위 편집은 영향을 받은 행과 문자 수만 갱신하며 영향을 받지 않은
  지속 인덱스를 보존한다. 런타임 계측 타이머는 남았다.

별도 회귀 확인에서 iiWebServer 호스팅 전체 검사의 프리뷰 한 항목은 숨김 테스트 상위
경로를 파일 제공자의 자동 프리뷰 정책이 제외하여 실패했다. 동일 항목은 공개 리소스
경로에서 통과했으나 기존 테스트 조건은 변경하지 않았다. iiSocietyHelper 전체 세션
검사에서는 자동 로컬 네트워크의 프리뷰 판본이 변경되어 한 항목이 실패했고, 동일 항목의
단독 재실행은 통과했다. 이 결과는 전체 세션 검사가 통과했다는 뜻이 아니다.

나머지 감사 항목은 미완료이다. 특히 파일 감시 유실·용량 초과의 재조정, 프로세스 종료와
취소의 깨움, 데몬과 장치별 알림, 라이선스 변경, 프레임 및 원격 공급자의 완료 경계는
기존 복구·권한·수명 보장을 유지하는 별도의 구현과 검증이 필요하다. SDK 간 새 링크
관계는 추가하지 않았다. 다만 iiWebServer와 LVUI는 기존에 직접 사용하던 iiSDKCore의
HostLoop·완료 신호 API를 추가로 사용하므로, 기존 모듈 간 API 결합과 해당 Core 기능에
대한 요구는 남는다. 감사에 기록한 기존 의존성 정리는 이 작업의 결과가 아니다.

## 2026-11-06 추가 수정과 남은 경계

- iiGUI의 Linux 기본값과 `AUTO`는 Wayland를 요구한다. Wayland 개발 패키지가
  없으면 X11로 전환하지 않고 구성 오류를 보고한다. X11은 명시적 선택만 가능한
  폐기 예정 경로이다. Windows 소스는 명시적 프레임 루프 또는 창의 모션 수요가
  있을 때 cadence를 유지하며 보조 창의 프레임 루프도 진행하도록 변경했다. macOS
  빌드에서 공통 프레임 수요와 백엔드 선택 정책을 검사하고 Windows x86-64·ARM64
  호스트 소스를 객체로 교차 컴파일했다. Windows 라이브러리 링크나 실행, Linux
  compositor와의 실제 연결은 검증하지 않았다.
- iiWebServer의 피어·LAN 피어·릴레이 만료 검사는 각각 실제 요청, 연결, QR,
  인증 및 경로의 기한에 예약하는 단발 타이머로 변경했다. WebSocket heartbeat는
  연결이 존재할 때 유지한다. 유휴 상태, 만료, 객체 파괴와 서버 중지 검사를
  포함한 호스팅·LAN·페어링 및 리소스 검사 여섯 건이 통과했다.
- iiWebServer 호스팅 픽스처는 일반 파일 프리뷰를 제외하는 숨김 상위 디렉터리
  대신 공개 리소스 작업 공간을 사용한다. 숨김 파일과 제어 파일을 제한하는
  기존 검사는 유지한다.
- iiSocietyHelper 프리뷰 전송은 이어받기 중 정보·원본 판본이 바뀌면 이전 조각을
  버리고 같은 전체 기한 안에서 최대 두 번 처음부터 다시 요청한다. 주입한 일회성
  충돌의 복구와 반복 충돌의 최종 오류, 원본 바이트 검사는 통과했다. 기존 실패
  행인 자동 로컬 네트워크 단독 검사도 통과했다. 다만 전체 세션 검사는 300초
  제한에 도달하여 통과하지 못했다. 제한과 기존 테스트를 완화하지 않았다.
- iiDeviceMultimedia의 Apple CoreMIDI 실행 루프는 25ms 간격 대신 알림이나 보존된
  종료 신호로 깨어난다. 대기 진입 직전 취소 및 스레드 종료 검사가 통과했다.
  MIDI 토폴로지 알림 누락에 대한 100ms 재조정과 호스트의 명시적 `poll()`은 남는다.
- iiFileProvider 패키징 CLI의 POSIX 경로는 종료 신호를 전용 대기로 받아 취소를
  요청하고 정상 종료에 대기 스레드를 합류시킨다. `SIGINT`·`SIGTERM` 및 유휴 종료
  검사와 기존 CLI의 충돌·재개·감사 검사가 통과했다. Windows 대체 경로의 50ms
  취소 확인은 아직 남아 있다.
- iiGeneralDocument의 POSIX 문서 변환은 출력 준비와 자식 종료를 실제 시간 제한과
  함께 기다린다. 자식이 출력 스트림을 먼저 닫아도 반복해서 깨어나지 않으며,
  자식 회수 전에 종료를 관찰하여 제한 시간의 프로세스 그룹 종료를 유지한다.
  출력 조기 종료의 CPU 사용, 별도 프로세스 보존과 시간 초과 검사가 통과했다.
  Windows 변환기 경로는 이 변경에 포함되지 않는다.

추가 조사에서 iiSocietyHelper의 `Client::receiveDrive()`와
`Synchronizer::receive()`가 비동기 호스트 작업의 `pending` 결과를 반복 조회하는
것도 확인했다. 이 경로를 제거하려면 인증된 전송의 완료 통지, 중복 요청과 응답
유실 시 재연결 복구, 기존 원격 호스트와의 명시적인 호환 정책이 필요하다.
Society 단독 검사에서 기존 설치 iiWebServer가 동적으로 적재된 사실을 확인했다.
따라서 그 결과는 현재 소스의 iiWebServer와 iiSocietyHelper를 함께 검증한 결과가
아니다. 현재 빌드된 iiWebServer와 Core를 지정한 전체 재검사도 기존 300초 제한에
도달했다. 같은 조합의 자동 로컬 네트워크 행은 별도 실행에서 201초에 통과했으나,
같은 버전의 기존 설치 Core와 새 API를 사용하는 Core 사이에는 ABI 위험이 있으므로
라이브러리 경로만 바꾼 실행으로 전체 소비자 호환성을 보증하지 않는다.

**전체 작업은 미완료이다.** 위에서 열거한 변경 외에 감사 표의 장치·파일 감시,
접근성, 에이전트, 프로세스 및 외부 공급자 항목이 남는다. iiWebServer가 추가로
사용한 iiSDKCore `HostLoop`, LVUI와 iiNetwork의 예약 완료 신호 API 의존성도
제거되지 않았다. 큐 포화 때 완료가 사라지거나 콜백 스레드가 바뀌지 않도록
독립적인 호스트 전달 계약과 전환 검증이 필요하다.
