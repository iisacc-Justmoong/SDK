# 헤더와 구현의 배치

공개 헤더는 대응 구현과 같은 소스 디렉터리에 둔다. 저장소에 공개 헤더만을 위한
`include/` 트리를 만들지 않는다. 네이티브 API와 호환 계층에도 같은 규칙을 적용한다.
헤더 전용 타입과 umbrella 헤더도 해당 구현 모듈의 소스 옆에 둔다.

기본 배치는 `src/<모듈명>/`이다. 예를 들어 `iiNetwork`의 `Peer.h`와 `Peer.cpp`는
모두 `src/iiNetwork/`에 있다. CMake 빌드 인터페이스는 `src/`를 제공하므로 소비자의
`#include <iiNetwork/Peer.h>`는 그대로 유지된다. 독립 배포되는 저장소는 이 레이아웃을
자체 CMake와 문서로 설명해야 하며, 상위 SDK 체크아웃에 대한 빌드 의존성을 추가하지 않는다.

## 2026-10-21 전환 범위

14개 모듈의 공개 헤더 64개를 옮기고, 공개 헤더 전용 트리 16곳을 제거했다.

| 모듈 | 소스와 공개 헤더의 위치 |
| --- | --- |
| LVUI | `src/LVUI/` |
| iiActorObject | `src/iiActorObject/` |
| iiCalendar | `src/iiCalendar/` |
| iiColorManager | `src/iiColorManager/` |
| iiGUI | `src/iiGUI/` |
| iiJson | `src/iiJson/` |
| iiLicenseManager | `src/iiLicenseManager/`, `Native/src/iiLicenseManager/` |
| iiNetwork | `src/iiNetwork/` |
| iiPhotoLibrary | `src/iiPhotoLibrary/` |
| iiSDKBuilder | `src/iiSDKBuilder/` |
| iiSocialPublish | `src/iiSocialPublish/` |
| iiUpdateManager | `src/iiUpdateManager/`, `Native/src/iiUpdateManager/` |
| iiAccountManager | 네이티브 API의 `Native/src/iiAccountManager/` |
| iiFileProvider | 파일 호환 API의 `Compatibility/src/` |

## 빌드와 패키지 계약

- 공개 include 이름, CMake 타깃과 ABI를 유지한다. 소스 배치 변경을 이유로 버전을 올리지 않는다.
- CMake의 소스 목록, 헤더 검색 경로, 테스트의 내부 헤더 참조, 플랫폼별 구현 및 검사 도구를 함께 변경한다.
- 설치에는 명시된 공개 헤더만 포함한다. 구현 파일과 내부 헤더를 디렉터리째 설치하지 않는다.
- 설치 패키지의 `include/`는 소비자용 표준 배치이다. 소스의 분리된 `include/` 트리와 구분한다.
- 컴파일러와 플랫폼에서 생성하는 export 헤더는 `build/`에 둔다. 외부 의존성의 소스와 설치 헤더는 재배치하지 않는다.
- 사용자 설치본을 수정하지 않고 `build/` 아래에 패키지를 스테이징하여 독립 소비자를 구성·빌드·실행한다.

## 검증

SDK 루트에서 `python3 -B tests/test_source_header_layout.py`를 실행한다. 이 검사는
자체 CMake 모듈에 분리된 `include/` 트리가 없는지, 전환한 공개 헤더가 실제 구현 옆에
존재하는지 확인한다. 검사 대상 목록은 `tests/fixtures/source-header-layout.json`이다.

배치 검사만으로 패키지 호환성을 주장하지 않는다. 변경한 각 모듈을 `build/`에서
재구성·빌드하고 기존 CTest를 실행한 후, 설치된 헤더를 모두 포함하는 독립 소비자를
검증한다. 현재 호스트의 실행 결과와 다른 플랫폼의 실행 여부는 구분하여 기록한다.

## 전환 검증 결과

2026-10-21 macOS arm64에서 다음 검증을 완료했다.

- 독립 패키지 12개의 재구성·빌드·설치 스테이징과 기존 CTest 137개가 통과했다.
- 설치된 공개 헤더 60개를 각각 독립 번역 단위로 컴파일하고 패키지 소비자 12개를 실행했다.
- 소스 형태로 제공되는 네이티브·호환 API 헤더 4개도 각각 독립 컴파일했다. 네 계층을 함께
  빌드한 네이티브 소비자가 통과했으며, CMake 타깃 그래프에서 Qt 의존성이 없는지 검사했다.
- 독립 소비자는 셸의 `CPATH`, `LIBRARY_PATH` 등 경로 보정을 제거한 상태로 새로 구성하고
  빌드했다. 설치 소비자의 검색 경로에 SDK 소스 디렉터리가 없는 것도 확인했다.
- 공개 헤더 64개의 내용은 이동 전과 바이트 단위로 동일하다. 설치 대상에는 명시된 공개
  헤더만 포함되며, 분리된 소스 `include/` 트리의 재등장을 막는 Python 검사 2개도 통과했다.

검증 기록은 Workspace의 `build/header-layout-20261021/summary.json`에 있다. 사용자 설치본과
앱은 갱신하지 않았으며, Windows·Linux·모바일·WASM에서의 실제 실행은 이 검증에 포함하지 않는다.
