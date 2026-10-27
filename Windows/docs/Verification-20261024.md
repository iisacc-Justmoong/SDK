# 2026-10-24 Windows SDK 검증

검증 완료 시각: 2026-10-24T23:57:26.297007+09:00

중단 기록을 확인한 뒤 GitHub 동기화 소스에서 Windows 실패 원인을 수정하고 전체 검증을 다시 수행하였다.
구성은 Windows x64, MSVC, Ninja, Release, C++23이다. 소스 변경, 테스트, 문서 변경을 함께 보존하였다.

활성 SDK **37개(36 CMake + 1 Node)**의 구성·빌드·테스트·설치가 통과하였다. CTest 통과는 **1783개**, Node 통과는 **6개**이다.
CTest의 사용 불가능한 가속 장치 열거 제외는 **4개**이며 통과 수에 포함하지 않았다.
LVRS Rust CLI 25개, 검증 도구 회귀 15개, Network 설치 소비자 30개, iiWhatsNew 설치 소비자 1개와 Dreamscapes 저장 소비자 2개도 별도 통과하였다.

## 프로젝트별 결과

| SDK | 통과 | 제외 | 구성별 테스트 |
| --- | ---: | ---: | --- |
| iiAccountManager | 6 | 0 | test: 6 |
| iiActorObject | 34 | 0 | test: 34 |
| iiAudioGen | 7 | 0 | test: 7 |
| iiCalendar | 14 | 0 | test: 14 |
| iiCameraIntegration | 26 | 0 | test: 26 |
| iiColorManager | 6 | 0 | test: 6 |
| iiCSMIDI | 4 | 0 | test: 4 |
| iiDecision | 38 | 4 | test: 38 |
| iiDeviceMultimedia | 6 | 0 | test: 6 |
| iiFileProvider | 50 | 0 | test: 49, compatibility-test: 1 |
| iiGeneralDocument | 31 | 0 | test: 31 |
| iiGraphicAdjust | 202 | 0 | test: 202 |
| iiGUI | 138 | 0 | test: 138 |
| iiHtmlBlock | 13 | 0 | test: 13 |
| iiJson | 2 | 0 | test: 2 |
| iiLicenseManager | 2 | 0 | test: 2 |
| iiLocalDiffusion | 111 | 0 | test: 111 |
| iiLocalLLM | 11 | 0 | test: 11 |
| iiPaintEngine | 49 | 0 | test: 49 |
| iiPhotoLibrary | 8 | 0 | test: 8 |
| iiSDKBuilder | 16 | 0 | test: 16 |
| iiSDKCore | 58 | 0 | test: 58 |
| iiWebServer | 16 | 0 | test: 16 |
| iiSharedCanvas | 89 | 0 | test: 67, native-test: 21, native-consumer-test: 1 |
| iiSocialPublish | 17 | 0 | test: 17 |
| iiSocietyContainer | 21 | 0 | test: 21 |
| iiSocietyHelper | 27 | 0 | test: 27 |
| iiUpdateManager | 4 | 0 | test: 4 |
| iiVoiceOver | 5 | 0 | test: 5 |
| iiWhatsNew | 1 | 0 | test: 1 |
| iiXml | 16 | 0 | test: 16 |
| LVRS | 593 | 0 | test: 529, qt-test: 64 |
| LVRS-Svelte | 6 | 0 | test: 6 |
| iiMultimodalAgent | 68 | 0 | test: 68 |
| iiNetwork | 46 | 0 | test: 46 |
| LVUI | 45 | 0 | test: 45 |
| iiImageCodec | 3 | 0 | test: 3 |

## 수정 및 검증 내용

- 공통 검증: DLL 검색 순서, 중복 PATH, 이동한 CMake 경로, 실패한 선행 SDK 차단과 변경 무효 상태의 전파를 보강하였다. 모든 추가 구성의 실제 테스트 결과를 검사한다.
- iiSDKCore: MSVC 전처리 위치 표식 검사의 반복 경로 해석을 줄였다. 프로세스 격리는 핸들 숫자 대신 실제 이벤트 상속 여부로 검증하고, 원시 Windows 생성의 양성 대조도 확인한다.
- LVUI·iiGUI·LVRS: 키워드 목록과 아이콘 대소문자, Windows 입력 수명, 0 크기 요소, 접근성 등록 순서, 팝업 정리와 테이블 스크롤 알림 재진입을 수정하였다. D3D 효과의 실제 실행 계수와 픽셀 검사를 유지한다.
- LVRS Qt 호환: UTF-8 LF 저장의 실제 바이트 위치를 보존하고 정규·기존 아이콘 URL을 함께 제공한다. 창 활성화와 실제 포커스 윤곽 프레임을 기다리며 캡처 검사를 활성화한다.
- iiFileProvider: NTFS 변경 순번으로 같은 크기·같은 파일 시각의 연속 외부 쓰기를 구별하고, 자체 미리보기 게시의 캐시 정규화를 보존한다. 64회 연속 덮어쓰기 회귀 검사를 수행한다.
- iiLocalDiffusion: CLI JSON flush, 실제 sparse 파일 생성, Windows Python DLL 검색, 새 이미지 결과 폴더의 게시와 미리보기 작업 정리를 수정하였다. Pillow 설치 뒤 이미지 관련 검사도 실행한다.
- iiNetwork·iiWebServer: Windows 인터페이스·인증서 API와 소켓 경로 검사를 수정하고, Windows 정상 종료 신호로 실제 요청 drain을 검증하였다.
- iiSharedCanvas: 예외 가능 메타데이터 저장 공간을 DB 커밋 전에 준비하고, 커밋 후에는 예외 없는 필드 대입으로 같은 Document 주소를 유지한다. Qt와 네이티브 구성 및 별도 설치 소비자가 모두 통과하였다.
- iiMultimodalAgent: 세션 기록의 헤더·본문·cursor 읽기를 provider SourceAccess로 보호한다. 비동기 미리보기와 16회 연속 새 검색, 실제 본문 변경의 cursor 무효화를 검사한다. 입력 큐와 세션 기록도 3회 연속 재실행하였다.
- SocietyContainer·Helper: 상대 경로 전체를 확인하여 Windows의 provider catalog·WAL·SHM이 최근 파일과 alias 목록에 노출되지 않도록 한다. 사용자 캔버스 수의 기존 기대값을 유지한다.
- SocietyContainer 저장 경계: 열린 읽기 스냅샷이 Qt manifest 교체를 액세스 거부로 실패시키는 재현을 확보하고 네이티브 provider 원자적 저장으로 전환하였다. 유지보수와 복제본 전환 64회, 미리보기 게시와 모델 참조 확인 각각 64회, 호스트 전환의 독립 fixture 16회를 검사한다. 모델의 실제 내용 변경은 계속 거절한다.
- SocietyHelper 프로세스 관찰: 상태 파일을 private staging에서 완성한 뒤 네이티브 교체로 게시하고, 삭제 공유가 있는 열린 스냅샷의 실제 EOF까지 제한적으로 읽는다. 파일이 존재하지만 잠시 읽히지 않는 상태를 철회로 오인하지 않으며 유효한 순번 증가만 timeout을 갱신한다. 열린 스냅샷, 불완전한 JSON의 복구, 실제 삭제, 정상 peer의 무철회와 세 프로세스 crash·restart를 함께 검사한다.
- iiUpdateManager: 신호 처리 중 객체 삭제의 수명을 유지하면서 비동기 미리보기 핸들 반환 후 fixture를 정리한다.
- iiWhatsNew: 라이브러리와 공개 CMake 언어 요구를 C++23으로 맞추고 설치 소비자를 실행하였다. 전체 CMake SDK 36개에서 실제 MSVC C++ latest 플래그를 확인한다.
- Dreamscapes: SDK 파일 생성 경계 검사에서 확인한 저장 경로를 공개 provider API로 변경하였다. 변경한 세 번역 단위의 컴파일과 보고서 저장·리뷰 내보내기 소비자 2개를 검증하였다. 전체 제품 앱 빌드 결과와 구분한다.

## 설치 및 재실행

기본 설치는 `~/.local/SDK/<SDK>`이다. LVRS Qt 호환 구성은 `LVRS/qt`, 네이티브 iiSharedCanvas는 `iiSharedCanvas/native`에 분리한다. 모든 CMake 설치 매니페스트의 실제 파일 존재를 확인하였다. Rust CLI도 기본 LVRS prefix의 bin에 설치하고 실행하였다.

Workspace 루트에서 다음 명령으로 전체 판정을 재현한다.

```powershell
. ./SDK/Windows/Enter-DevEnvironment.ps1
python -B SDK/Windows/Verify.py --report-dir build/maintenance/20261024-windows-sdk-final --resume
```

Rust CLI와 추가 소비자 실행 명령 및 로그는 작업 기록의 `verify-lvrs-cli.py`, `verify-consumer.py`, `consumers/`에 보존되어 있다. LVRS의 병행 실행 기록은 같은 도구·소스·검증된 선행 설치 지문을 확인한 뒤 합쳤으며 전체 검증을 다시 통과시켰다.

## 실행 범위와 제외

iiDecision의 Metal, MLX, OpenCL, ONNX 가속 장치 열거 4개는 해당 환경에서 제외되었다. CPU Laya·ONNX 검사는 실행하였다. 개별 Python·Qt 검사 내부의 POSIX·macOS 전용 조건 제외는 원래 로그에 구분되어 있으며 CTest 단위 통과 수와 개별 언어 내부 사례 수를 혼합하지 않는다.
저장소의 fixture·실제 소켓·문서 I/O·설치 소비자 실행 결과이다. 대형 실제 모델 추론, 외부 서비스 계정, 물리 장치의 별도 실연동 결과와 구분한다. 네이티브 LVRS의 원본 대응표는 81 partial·68 pending이며 Windows 빌드 통과를 전체 기능 이식 완료로 표시하지 않았다.
이미지 폴더 게시 검증은 새 출력 경로를 사용한다. NTFS 변경 순번 검증은 이 머신의 NTFS 실행 근거이며 변경 저널이 없는 파일 시스템의 기존 시각 기반 경로와 구분한다.

다음 9개 경로에는 GitHub 대응 저장소와 빌드 소스·구성이 없어 예약 경로로 보존하였다. 성공한 SDK로 계산하지 않았다.

Flamewall, Silentlocked, iiComputerHistory, iiComputerUse, iiEmailClient, iiGlobalSearch, iiPBRMaterial, iiVirtualizer, iiVolumetricSolidMesh

## 근거

- 전체 실행: `C:\Users\andud\Workspace\build\maintenance\20261024-windows-sdk-final\verification.json`
- 설치·소스 감사: `C:\Users\andud\Workspace\build\maintenance\20261024-windows-sdk-final\release-audit.json`
- 중단 이력: `build/maintenance/20261024-windows-sdk/STOPPED.txt`
- 초기 GitHub 동기화: `build/workspace-sync/final-report.json`
- 수정 전 실패와 재현 로그: `build/maintenance/20261024-windows-sdk-triage/`
- Git commit·push는 수행하지 않았다. 로컬 소스 변경과 설치 결과를 보존한다.

자동 승인 검토가 아래 시험 임시 파일 삭제를 정책상 차단하여 그대로 보존하였다. 삭제 우회는 수행하지 않았다.

`C:\Users\andud\Workspace\SDK\iiLocalDiffusion\build\inspection-ax1cl48q\large.safetensors` (41422499916 bytes)
