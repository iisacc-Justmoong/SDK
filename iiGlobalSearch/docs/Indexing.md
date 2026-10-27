# 비동기 컨테이너 색인과 검색

## 범위와 자료 원본

`iiGlobalSearch::Index`는 절대 경로인 정상 디렉터리형 `.societycontainer` 하나만 받는다. iiSocietyContainer의 `DirectoryStorage::isPackageDirectory`로 확장자, 정규 경로, 리디렉션과 과거 디스크 이미지 표식을 검사한다. 이 검사는 매니페스트 전체나 권한을 인증하지 않는다. 파일 경로는 iiSocietyContainer `FileTree`로 검사하고 심볼릭 링크·Windows 리파스 포인트를 따라가지 않는다. 숨김 파일·디렉터리와 iiFileProvider 제어 파일은 제외한다. 그 밖의 컨테이너 내 모든 보이는 디렉터리가 대상이며 Deleted를 임의로 제외하지 않는다.

각 디렉터리에 기존 iiFileProvider 카탈로그가 있으면 읽기 전용으로 페이지 단위 조회한다. 카탈로그에만 존재하는 발행 대기 분류도 검색할 수 있다. 등록되지 않은 파일의 `.iisacc.json`은 `FileCatalog::classificationDocument`로 검사한다. 둘 다 있는 경우 해당 스냅샷에서는 카탈로그 분류가 우선한다. `.<파일명>.iisacc-preview`가 있는 파일은 `Presentation::load`가 유효한 표시 정보를 검증한 경우 색인한다. 표시 정보의 이름·MIME·확장자·속성만 검색하며 프리뷰 바이트는 검색 색인에 넣지 않는다. 제공자의 현재 `Presentation::load`는 동반 프리뷰가 없는 확장 속성만으로 결과를 반환하지 않는다.

검색 대상은 이러한 카탈로그·분류·표시 정보 중 적어도 하나가 있는 파일이다. 분류 검사는 원본 payload를 읽지 않는다. 표시 정보 검증은 원본 앞·뒤의 최대 32 KiB씩과 관련 IISC WAL의 제한된 샘플, 최대 2 MiB의 프리뷰 문서를 읽을 수 있다. 원본을 디코딩·수정하거나 카탈로그를 생성하지 않는다. 기존 SQLite 카탈로그의 읽기 전용 연결은 SQLite의 통상적인 WAL/공유 메모리 조정을 사용할 수 있다.

## 호출과 일관성

```cpp
#include <GlobalSearch.h>
#include <chrono>

iiGlobalSearch::Index index("/absolute/Library.societycontainer");
index.start(); // 작업자에 제출하고 즉시 반환
auto partial = index.search("campaign portrait", 50);
index.waitFor(std::chrono::seconds(5)); // 선택적 대기
auto progress = index.progress();
auto matches = index.search("campaign portrait", 50);
// 필요하면 index.cancel(); 이후 waitFor()로 작업 종료를 확인한다.
```

`SearchProvider`는 앱이 다른 검색 구현으로 교체할 수 있는 추상 인터페이스이며 `Index`가 이를 구현한다. iiSDKCore `Runtime`의 재사용 작업자 한 개가 비동기 작업을 수행한다. 색인은 프로세스 내 SQLite FTS5 메모리에만 존재한다. 검색은 진행 중인 부분 결과도 반환한다. `start()`는 즉시 반환하고 기존 스냅샷을 숨긴다. 이전 색인의 해제와 새 색인의 초기화는 작업자에서 수행하며 기존 작업이 실행 중이면 `false`이다. 새 색인이 준비되기 전 검색은 빈 결과를 반환한다. `cancel()`은 다음 확인 지점에서 작업을 중지한다. SQLite 또는 파일 I/O 호출 자체를 강제 중단하지 않는다. 소멸자는 작업을 취소하고 작업자가 끝날 때까지 기다린다. 상태는 `idle`, `indexing`, `completed`, `cancelled`, `failed`이며 건너뛴 손상·접근 오류는 `skipped`와 첫 오류 설명에 표시된다. `completed`여도 `skipped`가 0보다 크면 완전한 인벤토리가 아니다.

선택적 진행 콜백은 작업자에서 내부 잠금 밖에서 호출된다. 신속히 반환해야 하며 콜백 안에서 색인 객체를 소멸하거나 해당 작업의 종료를 기다리면 안 된다. 결과를 UI 스레드에 전달하는 것은 앱의 책임이다. 검색은 반환 직전 파일의 존재와 비리디렉션 경로를 다시 확인하지만 파일 시스템 전체의 원자적 스냅샷은 아니다. 변경을 자동 감시하지 않으므로 최신 결과가 필요하면 다시 `start()`한다.

쿼리는 대소문자와 악센트를 정규화하는 SQLite `unicode61` 단어 접두사이며 공백으로 나눈 항목은 AND이다. SQL이나 FTS 연산자를 직접 실행하지 않는다. 파일 경로, 제목, 설명, 키워드, 사용자 필드 및 컬렉션, 표시 속성을 검색한다. Provider 카탈로그 경로는 정규화된 철자도 색인한다. 카탈로그가 없는 네이티브 파일명의 서로 다른 Unicode 정규화 형식이 모두 일치한다고 보장하지 않는다. 빈 검색어는 현재 결과를 나열한다. 최대 결과 수는 1,024개이며 페이지네이션은 아직 제공하지 않는다.

구조화 객체가 필요한 소비자는 `searchAnswer()`를 사용한다. 원본 payload를 즉시 읽지 않는 타입 참조와 비동기 적재 방법은 [메모리 내 구조화 검색 답변](Answers.md)을 따른다.

## CLI와 검증

`ii-global-search index <절대-패키지-경로> [검색어] [--limit 1..1024]`는 완료까지 기다린 뒤 진행 상태와 상대 경로 결과를 JSON으로 출력한다. 실패는 0이 아닌 종료 코드이다. C++ API와 CLI는 같은 구현을 사용한다.

`iiGlobalSearch.index`는 실제 Provider 카탈로그·발행 대기 분류·독립 동반 파일·표시 정보, 비동기 부분 결과, 동시 시작 거부, Unicode 검색, 손상과 컨테이너 경계, 삭제 및 재색인을 검사한다. 합성 컨테이너는 검사 종료 시 정리한다. `iiGlobalSearch.cli`는 JSON 결과와 잘못된 경로의 실패를 검사하는 별도 실행 검사이며 기본 CTest에서는 비활성화되어 있다. 해당 검사의 유지 리소스는 `resources/tests/`에 둔다.

`build/iiGlobalSearchBenchmark <절대-build-경로>`는 발행 대기 분류 300개의 합성 카탈로그를 만들고 첫 부분 결과와 전체 완료까지의 시간을 별도로 출력한다. 생성 시간은 측정에 포함하지 않는다. 파일 시스템 캐시가 데워진 합성 조건이며 실제 컨테이너의 지연이나 성능 보장을 의미하지 않는다.
