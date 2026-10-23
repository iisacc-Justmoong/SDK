# Windows SDK 전체 검증 기록 — 2026-10-20


> iiWebServer의 이 기록은 네트워크 전환 이전 커밋의 실행 결과이다. 현재 iiNetwork 전환의 Windows 실행 검증으로 재사용하지 않는다.
활성 SDK 36개를 의존성 순서로 Release 구성·빌드·실행·설치하였고, 전체 성공을 확인한 뒤 각 SDK 저장소를 커밋·푸시하였다. 아래 36개 원격 branch의 commit을 실제 조회하여 로컬 HEAD와 일치함을 확인하였다. Deprecated와 Utility 및 Product는 이번 범위에서 제외하였다.

검증 완료: `2026-10-20T02:58:56.591336+09:00`. 이 기록은 명시된 commit과 Windows 지원 구성의 실행 증거이다.

- CMake 실행 검사: 911개 통과, 0개 실패, 4개 skip이다.
- LVRS-Svelte: Node 검사 5개, Svelte 검사, catalog 빌드, preview HTTP 200 및 로컬 패키지 설치를 완료하였다.
- 공통 검증·저장소 동기화 회귀 검사: 38개 통과이다.
- iiSharedCanvas 설치본의 독립 Core 소비자 1개와 전체 소비자 28개: 29개 통과이다.
- iiLocalLLM Windows 입력 대기열: 27개 하위 검사로 구성된 suite가 20회 연속 통과하였다. 원자적 게시·잠금 경합·영속 오류의 원본 보존·취소를 포함한다.
- iiLocalLLM compaction: 원본 기록 보존·v1→v2 변환·잘린 checkpoint 복구·손상 기록 거부를 포함한 12개 하위 검사 suite가 20회 연속 통과하였다. 쓰기 파일을 닫은 뒤 preview를 취소하여 Windows의 원자적 교체 경합을 해결하였다.
- iiLocalLLM agent profile: 32개 background 작업의 시작 직후 상태·완료·소유권을 조회하는 회귀 검사를 포함한 10개 하위 검사 suite가 20회 연속 통과하였다. 공유 JSON의 누락 key 삽입과 상태 조회 사이의 경합은 시작 시 잠금 안에서 확보한 불변 사본으로 해결하였다.
- iiLocalLLM task: 네이티브 잠금 공유·취소, 16개 스레드의 생성, 12개 스레드의 claim 및 8개 프로세스의 생성·단일 claim을 포함한 17개 하위 검사 suite가 20회 연속 통과하였다. Windows 잠금 삭제 지연을 제한 시간 안에서 처리하며 자식의 JSON 응답은 명시적으로 flush하고 검증한다.

## 프로젝트별 결과와 원격 commit

| SDK | 실행 통과 | skip | 빌드·설치 | 원격에서 확인한 commit |
|---|---:|---:|---|---|
| iiSDKCore | 43 | 0 | 성공 | [8b0d4c2bc1](https://github.com/iisacc-Justmoong/iiSDKCore/commit/8b0d4c2bc13138de2f629abb14230c33e27bd1db) |
| LVRS-Svelte | 5 | 0 | 성공 | [4b39edb4e8](https://github.com/iisacc-Justmoong/LVRS-Svelte/commit/4b39edb4e853f8c42ebd05ac8f123cffd43c98d4) |
| iiActorObject | 1 | 0 | 성공 | [72f6bc8534](https://github.com/iisacc-Justmoong/iiActorObject/commit/72f6bc853416828302d707c40f9f1d38df794a4c) |
| iiColorManager | 6 | 0 | 성공 | [64f76abe80](https://github.com/iisacc-Justmoong/iiColorManager/commit/64f76abe80977fc7c7a4103d4850f89bf3bb75ee) |
| iiDeviceMultimedia | 6 | 0 | 성공 | [43f48d5802](https://github.com/iisacc-Justmoong/iiDeviceMultimedia/commit/43f48d5802fda5859c8c7f0cae40e4179462f655) |
| iiSocialPublish | 13 | 0 | 성공 | [44aad3cc69](https://github.com/iisacc-Justmoong/iiSocialPublish/commit/44aad3cc69975837cf234e213c744f1651f77202) |
| iiWhatsNew | 1 | 0 | 성공 | [1d687d7b0b](https://github.com/iisacc-Justmoong/iiWhatsNew/commit/1d687d7b0b6034eb87bd60916e9b9489af97ef44) |
| iiDecision | 27 | 4 | 성공 | [3986ab211c](https://github.com/iisacc-Justmoong/iiDecision/commit/3986ab211cd223b5e1f76af97545356086377ac3) |
| iiFileProvider | 37 | 0 | 성공 | [e352478ff8](https://github.com/iisacc-Justmoong/iiFileProvider/commit/e352478ff8e94e3c8131f67c6c58cab6076a56ee) |
| iiGUI | 19 | 0 | 성공 | [8f441fb151](https://github.com/iisacc-Justmoong/iiGUI/commit/8f441fb151e1f65a55ba7a350a7cabf6c57deac2) |
| iiJson | 2 | 0 | 성공 | [6241f772ca](https://github.com/iisacc-Justmoong/iiJson/commit/6241f772ca38f5b2edadf37e9a48a30578ae17cb) |
| LVRS | 59 | 0 | 성공 | [8a43d08337](https://github.com/iisacc-Justmoong/LVRS/commit/8a43d0833762bcd06bd5ad0c91add1623d3c69f8) |
| iiAudioGen | 7 | 0 | 성공 | [5937c36081](https://github.com/iisacc-Justmoong/iiAudioGen/commit/5937c360812a23d05b2b5068d0bb7bc0fbc38a10) |
| iiCSMIDI | 4 | 0 | 성공 | [d8e43e709d](https://github.com/iisacc-Justmoong/iiCSMIDI/commit/d8e43e709d98c32f43e2d1022524c440fe5d084f) |
| iiCalendar | 14 | 0 | 성공 | [99abe44fd1](https://github.com/iisacc-Justmoong/iiCalendar/commit/99abe44fd16fc362675db91f8ca5257e5eb0930c) |
| iiGeneralDocument | 30 | 0 | 성공 | [8c6f39949f](https://github.com/iisacc-Justmoong/iiGeneralDocument/commit/8c6f39949fccbe925e13a8eda3418e84ea870bfe) |
| iiGraphicAdjust | 202 | 0 | 성공 | [fd4aba0430](https://github.com/iisacc-Justmoong/iiGraphicAdjust/commit/fd4aba043026fd472429a39718b75957b91df4eb) |
| iiLocalDiffusion | 108 | 0 | 성공 | [8dfd987326](https://github.com/iisacc-Justmoong/iiLocalDiffusion/commit/8dfd987326d2853f61d939902deaa1dfb2671b29) |
| iiLocalLLM | 73 | 0 | 성공 | [2dac6095de](https://github.com/iisacc-Justmoong/iiLocalLLM/commit/2dac6095de8ccd7d18c1ed7c1100c8ef1d4b82ea) |
| iiPaintEngine | 48 | 0 | 성공 | [671518df9a](https://github.com/iisacc-Justmoong/iiPaintEngine/commit/671518df9a14fe923851ab37dd629dfbe3c78c04) |
| iiSDKBuilder | 14 | 0 | 성공 | [3ff70ba352](https://github.com/iisacc-Justmoong/iiSDKBuilder/commit/3ff70ba352a574d935e423378882b511503ee43c) |
| iiWebServer | 12 | 0 | 성공 | [b664c0e851](https://github.com/iisacc-Justmoong/iiWebServer/commit/b664c0e8515a0eae2e4c9fc553640096b7929749) |
| iiSocietyContainer | 19 | 0 | 성공 | [cc58fa0ee2](https://github.com/iisacc-Justmoong/iiSocietyContainer/commit/cc58fa0ee23232a09c786a616eae1662fd87a95a) |
| iiUpdateManager | 4 | 0 | 성공 | [5cb478584c](https://github.com/iisacc-Justmoong/iiUpdateManager/commit/5cb478584cb010f35c40159a0c055885c3371a0a) |
| iiVoiceOver | 5 | 0 | 성공 | [3295a89854](https://github.com/iisacc-Justmoong/iiVoiceOver/commit/3295a898546387f7f381986b4263ebf7908a90fe) |
| iiXml | 15 | 0 | 성공 | [c8fe80f6a8](https://github.com/iisacc-Justmoong/iiXml/commit/c8fe80f6a8fa449e33001d897658bc0065fafc4e) |
| iiAcountManager | 6 | 0 | 성공 | [85916f215a](https://github.com/iisacc-Justmoong/iiAcountManager/commit/85916f215af097fb56b7ba9693d47f559d63b97e) |
| iiCameraIntergration | 26 | 0 | 성공 | [ce960f269b](https://github.com/iisacc-Justmoong/iiCameraIntergration/commit/ce960f269b528ef35f0da8937afc6cf859a6a7ef) |
| iiHtmlBlock | 13 | 0 | 성공 | [1d8b1b367c](https://github.com/iisacc-Justmoong/iiHtmlBlock/commit/1d8b1b367c94dc97c6b9bc843507193579bd53b4) |
| iiSharedCanvas | 62 | 0 | 성공 | [0d55bfedd7](https://github.com/iisacc-Justmoong/iiSharedCanvas/commit/0d55bfedd7d4e9ed48d270fb04e8a2ead1f5b516) |
| iiSocietyGeneration | 1 | 0 | 성공 | [12d3a8b524](https://github.com/iisacc-Justmoong/iiSocietyGeneration/commit/12d3a8b524484a8e3dbd834656ffe40309c8c480) |
| iiSocietyMonitor | 2 | 0 | 성공 | [39d3d76688](https://github.com/iisacc-Justmoong/iiSocietyMonitor/commit/39d3d7668810739329da63466b118625c743cdf3) |
| iiSocietySync | 12 | 0 | 성공 | [22a1c73008](https://github.com/iisacc-Justmoong/iiSocietySync/commit/22a1c730088ad8b96832724f9eeb8e9835a9ee61) |
| iiLicenseManager | 2 | 0 | 성공 | [66c06ccc65](https://github.com/iisacc-Justmoong/iiLicenseManager/commit/66c06ccc65ad88c02abf856f88047cab342a6989) |
| iiPhotoLibrary | 8 | 0 | 성공 | [ca5f8f1812](https://github.com/iisacc-Justmoong/iiPhotoLibrary/commit/ca5f8f1812e595dc6e945a96233398d8aca3b4b2) |
| iiSocietyHelper | 10 | 0 | 성공 | [eca7b1eec3](https://github.com/iisacc-Justmoong/iiSocietyHelper/commit/eca7b1eec34c066ea169c503a137e3b5010ab2c6) |

## Windows 복구 사항

- 컴파일러 절대 경로와 실제 CMake cache를 검사하여 Release 설정이 Debug로 초기화되는 구성을 차단하였다. 지역화된 MSVC include 출력, UTF-8 manifest와 긴 경로 설정을 적용하였다. module file set이 없는 코드의 불필요한 모듈 스캔을 끄고, clean C++23 소비자 빌드와 header 변경 뒤 재컴파일·재링크를 회귀 검사하였다.
- ONNX Runtime의 import library와 DLL을 구분하고 일치하는 DLL을 실행 파일 옆에 배치하였다. iiDecision CPU Laya/ONNX와 iiLocalLLM 실행을 검증하였다.
- MLX·OpenBLAS DLL 설치 위치와 독립 MLX 소비자의 export 의존성을 수정하여 iiLocalDiffusion과 iiSocietyGeneration 실행을 복구하였다.
- Windows 대기열의 파일 공유 충돌은 제한 시간·취소·원자성을 유지하여 처리한다. 지속 오류의 원본 보존을 회귀 검사하였다. NTFS preview metadata의 일시적인 timestamp 변경을 정규화하고, 실제 동일 길이 내용 변경은 계속 감지하도록 회귀 검사하였다. Windows file clock과 process lifecycle 계약도 검사하였다.
- 서버의 DACL, 빈 rclone 설정 경로, SQLite DLL, TLS key 권한, Python 자식 환경 및 Job Object 수명 관리를 수정하였다. FTP/FTPS/FTPES, SSH, WebDAV, S3, restic, SMB2 등 지원되는 로컬 프로토콜의 실제 전송을 실행하였다. SMB2 검사용 고정 소스는 클라이언트의 서명 검증을 유지하면서 LOGOFF 응답 수명을 보정한다.
- LVRS 글꼴 측정과 리소스 URL, pixel sampling 및 capture 크기를 정리하였다. PNG 기준과 허용 오차는 유지하며 reference PNG 검사는 고정 software rasterizer에서 수행한다.
- 카메라 Unicode JSON과 update/preview cleanup, 사진 view settlement 및 설치 패키지 소비자를 검증하였다.

## 실행 범위

iiDecision의 `Acceleration.metal`, `Acceleration.mlx`, `Acceleration.opencl`, `Acceleration.onnx`는 사용할 수 없는 가속 장치 열거 검사 4개이다. 실제 CPU ONNX 및 Laya 실행 검사는 통과하였다. skip을 해당 장치의 실행 성공으로 계산하지 않는다.

iiXml, iiHtmlBlock, iiSharedCanvas는 각 Windows 지침에 명시한 정적 라이브러리 구성으로 검증하였다. iiLocalDiffusion은 MLX CPU fixture 구성이다. 프로젝트별 실제 CMake 옵션은 구조화된 결과에 보존하며, 다른 라이브러리 변형이나 비활성화된 선택 엔진의 실행을 대신하지 않는다.

고정 rclone Windows 바이너리는 NFS 서버를 제공하지 않는다. NFS 요청의 `unsupported_protocol` 반환을 검사하였으며 Windows NFS 파일 전송 성공을 주장하지 않는다. Unix의 NFS I/O 검사는 유지한다.

`Flamewall`, `iiComputerHistory`, `iiComputerUse`, `Silentlocked`는 소스 파일·빌드 정의·Git 저장소가 없는 자리 표시 디렉터리이다. 실행 가능한 프로젝트 36개와 구분하며 성공 수에 포함하지 않는다.

실제 계정의 클라우드 인증, 물리 카메라·오디오 장치, NAS 제조사별 동작, Dokan 실제 mount 및 대형 모델 가중치는 별도 검증 범위이다. 각 프로젝트 Windows 지침에 fixture·consumer 실행의 범위를 기록하였다.

## 환경과 재현

검증 머신은 Microsoft Windows 11 Home 26200 x64이다. 도구는 cmake version 4.4.4, Ninja 1.13.2, Python 3.14.8, Node v24.21.0, npm 11.19.0, .NET 10.0.401이며 MSVC 19.51 / Windows SDK 10.0.28000을 사용하였다. LongPathsEnabled=1이다.

[공통 Windows 검증](README.md)의 명령으로 전체 의존성 순회를 재현한다. 실패, 예상하지 않은 skip, 실행 0개, 소스 변경은 전체 커밋 조건을 닫는다. 도구 자체는 Git 작업을 수행하지 않는다.

[구조화된 결과](Verification-20261020.json)에 프로젝트별 실행 수, 검증한 소스 fingerprint, 공개 commit, 원격 commit과 설치 prefix를 기록하였다. 로컬 상세 로그·JUnit·원본 결과는 Workspace의 `build/maintenance/20261019-221920-sdk-recovery/final/`에 보관한다. Git에는 build 산출물을 포함하지 않는다.
