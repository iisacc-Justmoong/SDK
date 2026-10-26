# 비GUI Qt 독립 모듈과 이행 경계

2026-10-22 Dreamscapes와 SDK의 실제 소스·공개 계약·CMake 의존성을 조사하고, 누락된 기반 기능을 네 개 모듈에서 구현·확장했다. 공통 실행기는 기존 iiSDKCore를 재사용하며 파일·네트워크·코덱·문서 기능을 Core에 역수입하지 않는다. 이 문서는 대체 SDK 제공, 소비자 이행, GUI 어댑터 유지, 실행 검증을 구분한다.

## Qt 기능별 소유자

| Qt 영역 / 실제 사용 기능 | iiSDK 또는 표준 기반 | 현재 범위와 남은 이행 |
|---|---|---|
| QtCore 문자열, 바이트, 컨테이너, 시간 | `std::string`, span/vector/map/variant, chrono, filesystem | 독립 계약에는 표준 값을 사용한다. 기존 서비스·문서 공개 API의 QString/QVariant 타입은 소비자와 함께 이행해야 한다. |
| QObject 비GUI 작업, QtConcurrent, QThread/QTimer, QProcess | 기존 iiSDKCore Runtime, SerialQueue, TaskScope, QueryPort, EventLoop, Process | 중복 실행기를 만들지 않는다. Dreamscapes 외부 worker는 이미 Core를 사용한다. 모델 제출·관측의 QtConcurrent와 UI 전달 QObject는 여전히 남아 있다. |
| QtCore 파일·원자적 저장·임시 디렉터리 | 기존 iiFileProviderNative | 새 코드도 제공자의 CRUD와 원자적 저장을 사용한다. QFile/QSaveFile 사용처의 자동 이행을 의미하지 않는다. |
| QJsonDocument/Object/Array, QVariant 데이터 | 기존 iiJson + Core Value/Contracts | 새 JSON 엔진을 만들지 않는다. 기존 서비스 JSON 스키마, 누락/null, 숫자 범위, Qt 변환은 소비자 이행 시 명시해야 한다. |
| QXmlStreamReader/Writer | **iiXml::Native 추가** | UTF-8 pull 이벤트와 XML writer. 기존 QObject XML API와 분리하여 Qt 없이 구성·설치·소비한다. SVG/FCPXML 소비 코드 이행은 별도이다. |
| QSqlDatabase/QSqlQuery의 SQLite 사용 | **iiFileProviderNative 0.9.0 확장** | 표준 타입 연결·문장·바인딩·행·트랜잭션. 전역 Qt SQL 연결/드라이버가 없다. SQLite 외 SQL 드라이버와 ORM은 제공하지 않는다. Helper의 기존 Qt SQL 스키마·질의는 그대로 남아 있다. |
| QtGui의 비GUI PNG/JPEG 인코딩·디코딩 | **iiImageCodec 0.1.0 신설** | RGB/RGBA PNG, RGB JPEG, RGBA8 디코딩, 한도/오류. 워터마크·ICC/EXIF·고비트·모든 플러그인 포맷을 대체하지 않는다. |
| QtNetwork TCP/UDP/IPC, TLS, HTTP, 쿠키, 인증·프록시, REST | 기존 iiNetwork | 기존 모듈을 재사용한다. 세션 정책·계정 API·서버 프로토콜·애플리케이션 수명 이행은 상위 모듈 책임이다. |
| QtWebSockets | **iiNetwork 0.5.0 확장** | HTTP/1.1 upgrade, 명시적 서버 승인, 메시지·마스크·fragmentation·UTF-8·ping/pong·종료. TCP/TLS 스트림은 기존 API로 조합한다. Qt 서버/클라이언트 소비자의 이행은 아직 완료하지 않았다. |
| QtBluetooth BLE | 기존 iiDeviceMultimedia | Core와 독립된 동위 모듈의 central/GATT를 사용한다. 모든 Bluetooth 프로파일 및 모든 플랫폼의 물리 장치 지원을 주장하지 않는다. |
| QtMultimedia 카메라·PCM·장치 | 기존 iiCameraIntegration, iiDeviceMultimedia | 네이티브 장치 계약을 재사용한다. 동영상 demux/codec, 전체 Qt 미디어 세션 및 권한 브리지의 동등성은 미완료이다. |
| QPainter/Path, 폰트·텍스트 조판, PDF, SVG 렌더링 | iiPaintEngine / iiGraphicAdjust / iiColorManager의 독립 기능 + 별도 렌더링 경계 | QImage codec의 분리만으로 SharedCanvas의 오프스크린 벡터·텍스트·PDF QtGui 의존성을 제거할 수 없다. 실제 그래픽 계약과 기준 이미지 검증이 추가로 필요하다. |
| QStandardPaths/QSettings, locale/timezone, 파일 감시·OS 서비스 | 명시적 파일 root + iiJson/FileProvider, 기존 도메인·플랫폼 API | 저장 경로/권한/기존 사용자 데이터 이행을 먼저 정의해야 한다. 이번에 포괄적인 자동 Qt 호환 계층은 추가하지 않았다. |
| QtQml/Quick/Controls, 창·입력·화면 표시 | Qt/LVRS GUI 어댑터 | 현 제품 GUI의 명시적 의존성이다. 비GUI SDK의 공개 API와 분리한다. |

SDK에는 이미 JSON·네트워크·프로세스·런타임·파일·장치·색상·그래픽 계산 기반이 존재한다. Qt 모듈 개수와 동일한 수의 새 iiSDK를 만들거나 표준 컨테이너를 다시 구현하지 않는다. 상위 서비스와 도메인을 표준 값/메서드로 이행한 뒤 GUI 경계에서 Qt 값을 변환한다.

## 새 계약과 의존성

| 패키지 / 타깃 | 공개 헤더 | 직접 외부 구현 의존성 |
|---|---|---|
| iiImageCodec 0.1.0 / `iiImageCodec::iiImageCodec` | `iiImageCodec/ImageCodec.h` | libpng >= 1.6, libjpeg 호환 구현; zlib는 PNG 패키지를 통해 전달 |
| iiXmlNative 0.1.x / `iiXml::Native` | `iiXml/Xml.h` | libxml2 >= 2.9 |
| iiFileProviderNative 0.9.0 / `iiFileProvider::Native` | `iiFileProviderDatabase.h` | 기존 SQLite/플랫폼 파일 구현. 별도 상위 SDK 의존성 없음 |
| iiNetwork 0.5.0 / `iiNetwork::iiNetwork` | `iiNetwork/WebSocket.h` | 기존 Core Runtime, iiJson, libcurl, OpenSSL. 새 Qt 의존성 없음 |

각 기능의 상세 의미론·한도·예외 범위는 다음 문서가 소유한다.

- `iiImageCodec/docs/Contracts.md`
- `iiXml/Docs/native.md`
- `iiFileProvider/docs/NATIVE_DATABASE.md`
- `iiNetwork/docs/WebSocket.md`

외부 코덱/SQL/TLS 라이브러리를 전부 자체 구현했다는 주장은 하지 않는다. 새 public API는 해당 C 라이브러리의 포인터·핸들·Qt 타입을 노출하지 않아 구현 교체 경계를 유지한다. 실제 장치·외부 서비스·다른 운영체제의 실행은 별도 증거가 필요하다.

## 실제 Dreamscapes 채택

`DreamscapesNativeGeneration`을 별도의 C++23 타깃으로 분리했다. `NativeGenerationWork`의 디렉터리와 프리뷰 파일 이름은 filesystem path와 std::string으로 전달한다. PNG 인코딩은 iiImageCodec, 원자적 저장은 iiFileProviderNative가 맡는다. QString/QImage/QFileInfo/QIODevice와 Qt 저장 장치를 worker에서 제거했다. Qt 변환은 GenerationExecution/GenerationProgress의 GUI 전달 경계에 둔다. provider 요청, 취소 토큰, 소유 수명, 단조 프리뷰 순서와 잠금 밖 통지는 유지한다.

`tests/native-generation`은 실제 앱 worker 소스와 SDK 헤더를 Qt 패키지 없이 직접 컴파일한다. 가짜 추론 provider가 만든 실제 RGB를 PNG로 저장·다시 디코딩하고, 중복/이전/잘못된 프레임, 취소, source lease 제거, provider 예외를 검증한다. 별도의 모델 엔진이나 작업별 실행 스레드를 추가하지 않는다.

앱 전체에서 QtConcurrent, 서비스·문서의 Qt 타입, 기존 스토리지 어댑터, 그래픽/미디어 코드가 제거된 것은 아니다. XML·SQL·WebSocket 모듈의 제공을 해당 상위 소비자들의 전체 이행으로 표현해서는 안 된다. 새 SDK 연결은 GUI FPS 개선 또는 앱 크기 감소의 측정값이 아니다.

## 재현 가능한 검증

모든 구성/컴파일 출력은 각 프로젝트의 `build/`에 둔다. 공통 설치 스테이지와 결과는 `SDK/build/qt-independence-20261022/`이다. 설치본 `~/.local`이나 다른 사용자의 dirty 소스, 실행 앱 및 실제 Society 저장소를 바꾸지 않는다.

1. 모듈 단위 빌드와 전체 계약 테스트를 실행한다.
2. iiXml은 `IIXML_BUILD_QT_API=OFF`의 독립 구성과 ON의 기존 API 회귀를 구분한다.
3. CMake 설치 패키지를 Workspace 스테이지에 출력한다.
4. `SDK/tests/qtfree`는 설치 패키지로 JSON → PNG → 파일/SQLite → XML → WebSocket 데이터 경로 및 실제 Dreamscapes worker를 빌드·실행한다. Qt5/Qt6 검색을 비활성화하며 Qt 타깃이 생기면 구성을 거절한다.
5. 실제 실행에서 dyld 로딩 기록과 direct dependencies를 확인한다. 소스 include 검사만으로 Qt 독립을 주장하지 않는다.
6. 같은 소비자를 `IISDK_QTFREE_SOURCE_MODULES=ON`, `IISDK_QTFREE_SANITIZERS=ON`으로 구성하면 변경된 네 모듈과 실제 worker를 함께 ASan/UBSan으로 컴파일한다. 외부 코덱·기존 설치 Core/JSON은 계측되지 않는다.
7. Dreamscapes는 기존 `build/bin/Dreamscapes.app` 하나만 빌드하고 프리뷰·생성·취소·패키지 시작 회귀를 확인한다. 설치·재시작·실제 모델 실행은 별도이다.

호스트 셸의 `DYLD_LIBRARY_PATH`가 오래된 동일 SONAME SDK를 강제로 불러오는 것을 확인했다. Native 테스트에는 해당 재정의만 제거하고 CMake build RPATH를 사용하는 환경을 명시했다. 환경 때문에 새 API가 없는 0.8.1 dylib을 불러온 실패와 코드 테스트 결과를 구분한다. 전역 셸 설정은 변경하지 않았다.

`tools/audit_qt_dependencies.py`는 build/·모델을 탐색하지 않고 C++ 소스 루트와 CMake 선언을 인벤토리한다. 미추적 소스도 포함하며 test 여부를 기록하고, 대소문자 비구분 파일 시스템의 src/Src 중복도 제거한다. 이 목록은 조건부 선언을 포함하는 정적 후보 목록이며 실제 활성 링크 그래프나 GUI/비GUI 의미 분석의 대체물이 아니다.

## 검토에 사용한 일차 자료

- [SQLite prepared statements](https://www.sqlite.org/c3ref/stmt.html), [step](https://www.sqlite.org/c3ref/step.html), [close](https://www.sqlite.org/c3ref/close.html)
- [libxml2 reader API](https://gnome.pages.gitlab.gnome.org/libxml2/html/xmlreader_8h.html), [parser options](https://gnome.pages.gitlab.gnome.org/libxml2/html/parser_8h.html)
- [libpng public API](https://github.com/pnggroup/libpng/blob/libpng18/png.h) 및 빌드에서 실제 사용한 1.6.59 헤더
- [RFC 6455](https://www.rfc-editor.org/rfc/rfc6455)

## 2026-10-22 완료 검증 결과

| 검증 | 결과 |
|---|---|
| iiImageCodec 계약 | 1/1 그룹 통과 |
| iiFileProviderNative 전체 | 새 SQLite를 포함한 5/5 그룹 통과 |
| iiXml Native 전용 구성 | 1/1 그룹 통과 |
| iiXml 기존 Qt API + Native 구성 | 16/16 그룹 통과 |
| iiNetwork 전체 | 새 WebSocket 4개 그룹을 포함한 28/28 통과 |
| 설치된 SDK 패키지만 사용하는 독립 소비자 | 2/2 통과, 실제 로딩된 Qt 라이브러리 각각 0개 |
| ASan + UBSan 소스 결합 구성 | 변경 모듈·실제 worker의 생산 번역 단위 31개 계측, 35/35 그룹 통과 |
| Dreamscapes 선택 회귀 | NativeGeneration / DependencyBoundary / LocalSociety / PackagedApplication / Generation / GenerationProcess를 한 번에 실행하여 6/6 통과 |
| SDK 소스 헤더 배치 / 인벤토리 도구 | 기존 2개 + 새 1개 Python 테스트 통과 |
| SDK 선언 의존성 | 37개 노드의 선언 그래프에 순환 없음, FileProvider의 상위 SDK 참조 없음 |
| 앱 산출물 | 버전·빌드 번호 0.1.1, canonical 앱 1개, deep/strict 서명 검사 통과 |

각 행은 서로 다른 검증 모드의 테스트 그룹 수이며 합산한 고유 기능 수가 아니다. ASan 구성은 leak 검출을 비활성화했으며 외부 PNG/JPEG/libxml2/SQLite/OpenSSL 및 이미 설치된 Core/JSON 바이너리는 계측하지 않았다. macOS arm64 실행 결과이며 다른 OS와 물리 장치 검증을 대신하지 않는다.

초기 Dreamscapes 회귀에서 백그라운드/진행률 시간 조건의 실패를 관측했다. 같은 코드와 기존 200/300ms watchdog 및 테스트 제한을 유지한 채 개별 행, LocalSociety 전체, 최종 6개 그룹 연속 실행 순서로 재검증했고 마지막 실행은 모두 통과했다. 초기의 실패한 재시도 기록도 보존하며 시간 민감성의 원인을 확정했다고 주장하지 않는다. 실제 모델/원격 모델을 지정하는 선택 테스트는 수행하지 않았다. 전체 GUI 회귀 110여 그룹을 이번 작업에서 다시 실행한 것은 아니다.

설치 스테이지는 `SDK/build/qt-independence-20261022/prefix`, 최종 결과 원본·실행 로딩 기록·소스/패키지 해시는 같은 경로의 `final/`이다. 전역 SDK 설치, Git commit/push 및 `/Applications` 재설치는 수행하지 않았다. Dreamscapes의 빌드 후보만 갱신했으며 실행 중인 사용자의 앱/작업을 재시작하지 않았다.
