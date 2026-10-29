# 헤더와 구현의 배치

공개 헤더는 대응 구현과 같은 소스 디렉터리에 둔다. 저장소에 공개 헤더만을 위한
`include/` 트리를 만들지 않는다. 네이티브 API와 호환 계층에도 같은 규칙을 적용한다.
헤더 전용 타입과 umbrella 헤더도 해당 구현 모듈의 소스 옆에 둔다.

기본 배치는 `src/` 바로 아래이며, 파일이 많으면 기능과 책임에 따라 그 아래에
디렉터리를 둔다. 프로젝트 이름을 한 번 더 반복하는 디렉터리는 두지 않는다.
공통 검사는 `src/`와 별도 네이티브 구현의 `Native/src/`를 함께 확인한다.
확장자가 없는 공개 헤더 파일은 같은 이름의 디렉터리가 아니므로 허용한다.
모듈 통합 및 계정 이름 검사도 각 책임 디렉터리의 실제 소스 위치를 참조한다.
소스 위치와 설치 패키지의 공개 include 이름은 별개의 계약이다. CMake가 필요한
빌드용 전달 헤더를 생성하고 실제 공개 헤더를 기존 패키지 경로에 설치할 수 있다.
각 독립 저장소는 자체 CMake와 문서로 구조를 설명하며 상위 SDK 체크아웃에 대한
빌드 의존성을 추가하지 않는다. 모듈별 분류와 공개 헤더 검사는 해당 모듈 문서와 테스트에서 유지한다.

## 2026-10-29 소스 구조 정리

30개 모듈을 정리하고 검사 대상과 대표 공개 헤더를 `tests/fixtures/source-header-layout.json`에 기록했다.
파일 수가 적고 책임이 하나인 모듈은 `src/` 바로 아래에 두고, 복잡한 모듈은 책임에 따라 나누었다.
예를 들어 iiNetwork는 전송·세션·응용 계층으로, iiGUI는 그래픽·플랫폼·위젯 등으로 나눈다.
iiFileProvider는 API·파일·카탈로그·객체 저장을 구분하며 네이티브 구현도 별도로 분류한다.
LVUI, LVRS, iiActorObject, iiSocietyHelper 등의 세부 위치는 각 모듈 문서를 따른다.

macOS arm64에서 변경된 모듈의 빌드, 관련 자동 테스트와 설치 패키지를 사용하는 소비자를 검증했다.
사용자 설치본은 이 작업에서 교체하지 않았다. 다른 운영체제의 실제 실행이나 모든 하드웨어 기능은
별도 검증이 필요하다.

## 2026-10-21 전환 범위

당시 14개 모듈의 공개 헤더 64개를 옮기고, 공개 헤더 전용 트리 16곳을 제거했다.
다음 표는 당시 기록이며 현재 경로는 각 모듈 문서와 테스트 목록을 따른다.

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

- 공개 include 이름과 CMake 타깃을 유지하고 기존 ABI 유지를 목표로 한다. 소스 배치 변경을 이유로 버전을 올리지 않는다.
- iiGUI와 LVRS처럼 공개 헤더에서 ABI 식별자를 계산하는 패키지는 식별자가 달라질 수 있다. 의존 패키지는 일치하는 빌드와 함께 다시 구성한다.
- CMake의 소스 목록, 헤더 검색 경로, 테스트의 내부 헤더 참조, 플랫폼별 구현 및 검사 도구를 함께 변경한다.
- 설치에는 명시된 공개 헤더만 포함한다. 구현 파일과 내부 헤더를 디렉터리째 설치하지 않는다.
- 설치 패키지의 `include/`는 소비자용 표준 배치이다. 소스의 분리된 `include/` 트리와 구분한다.
- 컴파일러와 플랫폼에서 생성하는 export 헤더는 `build/`에 둔다. 외부 의존성의 소스와 설치 헤더는 재배치하지 않는다.
- 사용자 설치본을 수정하지 않고 `build/` 아래에 패키지를 스테이징하여 독립 소비자를 구성·빌드·실행한다.

## 검증

SDK 루트에서 `python3 -B tests/test_source_header_layout.py`를 실행한다. 이 검사는
자체 CMake 모듈에 분리된 `include/` 트리가 없는지, 전환한 공개 헤더가 실제 구현 옆에
존재하는지 확인한다. CMake 사용 여부에 관계없이 SDK 소스 모듈의 `src/<모듈명>/`과
`Native/src/<모듈명>/`이 다시 생기지 않았는지도 확인한다. 외부 의존성이나 설치 검증 자료를 담는 `resources/`는 자체
소스 트리 검사에서 제외한다. 검사 대상 목록은 `tests/fixtures/source-header-layout.json`이다.

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
