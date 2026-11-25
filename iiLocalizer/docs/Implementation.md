# 현재 구현과 사용 방법

## 구현 범위

iiLocalizer는 다른 SDK에 링크하지 않는 C++23 라이브러리이다. UTF-8 PO 바이트를
받아 불변 카탈로그로 파싱하고 단수 원문과 선택적인 문맥으로 번역을 찾는다.
결과에 `%1`, `%2` 등의 텍스트 인자를 치환한다. C ABI 및 이를 사용하는 Python
API는 같은 네이티브 판독기와 치환기를 호출한다. 카탈로그 자체는 언어를 선택하거나
파일을 열지 않으며, 같은 불변 카탈로그는 독립적으로 복사하거나 읽을 수 있다.

현재 복수 PO 항목은 오류로 거부한다. 도메인·복수형 수식·언어 선호 목록과 자동
대체, 번역 추출, `.mo`, 시스템 로캘 감지, 변경 구독은 아직 구현되지 않았다.
`Plural-Forms` 헤더는 단수 PO에서 무시하며 복수형 동작의 지원을 뜻하지 않는다.
LVUI 예제의 언어별 카탈로그 선택과 바인딩 갱신은 앱 어댑터와 LVUI가 담당한다.
예제 어댑터는 원문의 사용 위치와 PO 문맥을 독립적으로 지정하거나 생략하여
게시한다. 언어 또는 인자가 바뀔 때 호스트가 해당 언어의 값을 다시 게시해야 한다.

PO는 8MiB 이하, 항목은 헤더를 제외하고 최대 10000개, 각 필드와 인자는 65536바이트
이하, 인자는 최대 1024개, 치환 결과는 8MiB 이하여야 한다. UTF-8 이외의 인코딩,
NUL, UTF-8 BOM, 중복 메시지 식별, 잘못된 문법과 미지원 지시어를 거부한다.
`Content-Type`에 문자 집합이 선언되면 `UTF-8`이어야 한다. 헤더의 `Language`는
그대로 제공하며 검증·언어 선택은 호스트가 한다. 주석과 `#,` 또는 `#=`의 `fuzzy`,
`#~` obsolete 항목을 처리한다. 빈 번역은 미번역으로 취급한다.

인식하는 문자열 이스케이프는 `\a`, `\b`, `\f`, `\n`, `\r`, `\t`, `\v`,
`\\`, `\"`, 최대 세 자리의 8진수 및 한 바이트 범위의 16진수 `\x`이다. 여러
줄에 걸친 PO 문자열은 이어 붙인다. 지원하지 않는 이스케이프와 0 바이트는 거부한다.
GNU PO의 모든 확장에 대한 호환성 주장은 아니다.

## C++와 C

설치된 모듈은 `#include <iiLocalizer>`와 CMake 타깃
`iiLocalizer::iiLocalizer`를 제공한다. `IILOCALIZER_BUILD_TESTS`는 모듈 자체의
테스트를 제어하며 설치된 `iilocalizer_link(소비자_타깃)`도 사용할 수 있다.
파일 읽기와 언어 판단은 아래 `po_text`를 제공하는 호출자의 책임이다.

```cpp
#include <iiLocalizer>
#include <array>

const auto catalog = ii::localizer::Catalog::parse(po_text);
if (catalog) {
    const std::array<std::string, 1> args{"지민"};
    const auto translated = catalog->translate("Hello, %1", args);
    if (translated) {
        // translated->text와 translated->origin을 사용한다.
    }
} else {
    // catalog.error().message와 line을 호스트 진단에 전달한다.
}
```

문맥은 `translate("Open", {}, "menu")`처럼 전달한다. 문맥 생략과 빈 문맥은
별개이다. 번역이 없으면 `Origin::source_missing`, 번역의 인자가 잘못되어 원문을
사용하면 `Origin::source_invalid_translation`을 반환한다. 원문도 유효하지 않으면
오류이다. 인자로 전달된 문자열은 다시 치환하지 않는다. `iiLocalizerC.h`는 C ABI와
결과 문자열의 소유권·해제 방법을 정의한다.

## 같은 원문을 쓰는 레이블 선택

호스트는 각 레이블에 고유한 사용 위치, 원문, 문자열 키를 등록한다. 선택 위치를
생략하면 해당 원문의 모든 등록 사용 인덱스를 반환하고, 위치를 전달하면 그 위치와
원문이 모두 일치하는 인덱스만 반환한다. 일치 항목이 없으면 빈 목록이다. 빈 위치,
중복 위치, 중복 문자열 키와 잘못된 UTF-8은 오류이다. 위치는 대소문자와 전체
문자열이 정확히 일치해야 하며 상위 경로만으로 자식을 선택하지 않는다.

```cpp
#include <iiLocalizer>
#include <vector>

const std::vector<ii::localizer::LabelUsage> uses{
    {"main/toolbar/toolbarGreeting", "Hello, %1", "toolbar.greeting"},
    {"main/panel/panelGreeting", "Hello, %1", "panel.greeting"},
};
const auto all = ii::localizer::select_label_usages(uses, "Hello, %1");
const auto one = ii::localizer::select_label_usages(
    uses, "Hello, %1", "main/panel/panelGreeting");
// all은 [0, 1], one은 [1]에 해당한다.
```

예제의 `LabelAdapter::publish`는 사용 위치를 네 번째 선택 인자로 받는다. 위치를
생략하면 등록된 동일 원문 전체, 위치를 주면 해당 레이블만 `AppStrings`에서
교체한다. 다섯 번째 선택 인자로 PO `msgctxt`를 별도로 지정할 수 있다. 예를 들어
메뉴의 `Open`에 `publish("ko", catalog, {}, "main/toolbar/toolbarOpen", "menu")`를
호출하면 해당 위치에서 `menu` 문맥의 번역을 사용한다. 예제 어댑터는 선택 결과가
없을 때 `not_found`를 반환하며 기존 값을 보존한다.

LVUI는 각 레이블에서 등록된 고유 키를 `localized(...)`로 읽어야 한다. 모듈은
등록되지 않은 뷰를 발견하거나 실제 키 사용을 검증하지 못하므로 앱은 등록을
완전하게 유지하고 다른 레이블이 같은 키를 사용하지 않도록 보장해야 한다.
위치를 생략한 전체 선택의 범위도 그 등록 목록이다. 기존 단일 공유 키 예제는
위치 없는 호출을 유지하지만, 고유한 사용 위치가 없으므로 지정 호출은 거부한다.
등록은 최대 10000개이며 각 필드는 65536바이트, 질의와 등록 필드를 합친 크기는
8MiB를 넘을 수 없다. 사용 위치는 파일 시스템 경로가 아니라 호스트가 정한
불투명한 식별자이다.

## Python

설치된 `lib/python`을 Python 모듈 검색 경로에 추가한다. 공유 라이브러리는 설치된
`lib`에서 찾으며 필요하면 `IILOCALIZER_LIBRARY`로 명시할 수 있다.

```python
from iilocalizer import Catalog

with Catalog.from_po(po_text) as catalog:
    text = catalog.translate("Hello, %1", ["지민"])
    detail = catalog.translate_info("Hello, %1", ["지민"])
    # detail.text와 detail.origin을 사용할 수 있다.
```

```python
from iilocalizer import LabelUsage, select_label_usages

uses = [
    LabelUsage("main/toolbar/toolbarGreeting", "Hello, %1", "toolbar.greeting"),
    LabelUsage("main/panel/panelGreeting", "Hello, %1", "panel.greeting"),
]
all_uses = select_label_usages(uses, "Hello, %1")
panel = select_label_usages(uses, "Hello, %1", "main/panel/panelGreeting")
# all_uses는 [0, 1], panel은 [1]이다.
```

잘못된 PO 또는 유효하지 않은 인자는 `LocalizationError`이다. `close()` 또는
`with` 종료 후에는 더 조회할 수 없다. 여러 Python 호출과 `close()`는 같은 객체
안에서 동기화된다.

## 빌드와 검사

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DIILOCALIZER_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/build/stage"
```

`install.sh`는 기본적으로 `~/.local/SDK/iiLocalizer`에 설치하며
`IILOCALIZER_PREFIX`로 변경할 수 있다. 위 명령의 `build/stage/`는 사용자 설치를
변경하지 않는 소비자 검증용 경로이다.

`example/LVUILabel`은 별도의 앱 소비자 프로젝트이다. `iiLocalizer`, LVUI, iiGUI,
iiSDKCore와 그 의존성을 CMake에서 찾을 수 있도록 각 설치 접두 경로를 제공한 뒤
아래처럼 구성한다. `IIGUI_SOURCE_DIR`은 창 없이 위젯 상태를 검사하는 테스트
하네스가 사용하는 iiGUI 소스 경로이며 기본값은 형제 `SDK/iiGUI`이다.

```sh
cmake -S example/LVUILabel -B build/lvui-label \
  -DiiLocalizer_DIR="$PWD/build/stage/lib/cmake/iiLocalizer" \
  -DCMAKE_PREFIX_PATH="설치된 SDK 접두 경로 목록"
cmake --build build/lvui-label
ctest --test-dir build/lvui-label --output-on-failure
```

2026-11-24 macOS Apple Silicon에서 C++·Python 계약 테스트와 별도 LVUI 소비자
테스트가 통과했다. GNU `msgfmt`가 있으면 같은 PO의 단수·문맥·미번역 조회를 GNU
gettext 결과와 비교하는 테스트도 실행하며, 이 환경에서 8개 조회가 일치했다.
소비자는 `.lvui`를 실제 AOT 컴파일하고 iiGUI의 헤드리스
레이블 상태에서 언어 전환과 인자 변경을 검사했다. 운영체제 창 표시, 실제 제품
통합, 다른 운영체제 및 전체 현지화 계약의 완료는 검증하지 않았다.
