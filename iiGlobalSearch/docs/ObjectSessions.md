# 파일 객체 검색 세션

`ObjectSession`은 파일 제공자가 넘긴 식별자, 유형, 분류 메타데이터와
표시 상세 정보의 **프로세스 내 검색 세션**이다. 제공자의 파일·카탈로그를
수정하거나 미디어 페이로드를 복사하지 않는다. iiFileProvider `ObjectStore`의
영구 작업 세션 및 저널과는 다른 수명이며 그것들을 종료하거나 삭제하지 않는다.
공개 API는 iiFileProvider 타입을 노출하지 않으므로 앱은 다른 제공자의 값도
같은 구조로 전달할 수 있다.

## 세션 생성과 명시적 파괴

```cpp
#include <GlobalSearch.h>
#include <chrono>

iiGlobalSearch::Index index("/absolute/Library.societycontainer");
if (!index.start() || !index.waitFor(std::chrono::seconds(30)) ||
    index.progress().state != iiGlobalSearch::State::completed)
    return 1;

auto session = index.openSession();
iiGlobalSearch::ObjectFilter query;
query.text = "campaign portrait";
query.types = {iiGlobalSearch::ObjectType::image};
query.minimumRating = 4;
query.allKeywords = {"approved"};
auto objects = session->search(query, 50);
// 객체의 id, providerId, mimeType, metadata와 presentation을 사용한다.
session->destroy(); // 사용자가 세션 사용을 마칠 때 명시적으로 호출
```

`openSession()`은 완료된 색인에서만 가능하다. 색인에 건너뛴 항목이 있을 수
있으므로 먼저 `progress().skipped`를 확인한다. 세션은 완료 시점의 스냅샷을
공유하며 `Index`가 새로 고쳐지거나 먼저 소멸해도 유지된다. 세션 생성 이후
원본 메타데이터가 바뀌면 새 색인과 새 세션이 필요하다. 원본 파일이 지워지거나
리디렉션된 경우 각 조회의 반환 직전에 기존 컨테이너 경계를 다시 검사한다.
이는 파일 시스템 전체의 원자적 스냅샷이나 장기적인 파일 핸들 보장이 아니다.

`destroy()`는 진행 중인 세션 연산이 끝날 때까지 직렬화하여 기다린 뒤 세션이
소유한 스냅샷 참조를 놓고 다시 쓸 수 없도록 한다. 반복 호출은 안전하다.
종료 후 `find`, `search`, `store`는 `std::logic_error`를 던진다. 소멸자는
누락에 대비해 같은 정리를 수행하지만 호출자는 정상 경로에서 명시적으로
`destroy()`를 호출해야 한다. 호출자가 이미 받은 `FileObject` 사본은 호출자
소유이다. 같은 스냅샷을 가진 `Index`나 다른 세션이 살아 있으면 그들의
색인은 별도로 유지된다. 객체 자체를 파괴하는 동안 다른 스레드가 그 C++
객체를 역참조하는 것은 허용되지 않는다.

## 제공자 값 직접 저장

```cpp
iiGlobalSearch::ObjectSession session;
iiGlobalSearch::FileObject file;
file.id = "photos/cover.png";      // 이 세션 안에서 유일한 ID
file.providerId = "provider-key";  // 제공자의 원본 식별자
file.relativePath = "photos/cover.png";
file.title = "Cover";
file.mimeType = "image/png";
file.metadata.keywords = {"approved"};
file.metadata.rating = 4;
file.metadata.fields = {{"client", "Aurora"}};
file.hasClassification = true;
session.store(std::move(file));
auto found = session.find("photos/cover.png");
session.destroy();
```

앱의 어댑터는 iiFileProvider `CatalogFile`의 `key`, `path`, `size`,
`metadata` 및 `PresentationRecord::details`를 이 구조로 옮길 수 있다.
`Index::openSession()` 경로에서는 이 변환이 자동으로 이뤄진다. `id`는 세션
내 식별자이며 컨테이너 색인에서는 상대 경로이고 `providerId`는 원본
`fileKey`이다. 제공자 키가 없는 표시 정보 전용 파일의 `providerId`는 비어
있을 수 있다. 카탈로그에서 가져온 `revision`, `contentVersion`, `modifiedAtNs`도
보존한다. 독립 동반 파일에는 제공되지 않은 값이 있을 수 있다. 원본 분류 문서는 `classification`, 분류 메타데이터는
`metadata`, 표시 속성·미리보기 유형과 크기는 `presentation`에서 확인한다.
`metadata.additional`은 임의의 구조화 값을 보존한다. 프리뷰 바이트는
포함하지 않는다.

직접 생성한 세션은 `store()`로 서로 다른 ID를 추가한다. 중복 ID는 거부하며
갱신된 공급자 데이터는 새 세션으로 교체한다. 컨테이너에서 연 세션은 읽기
전용이다. 직접 공급한 값은 파일 시스템을 열거나 접근 권한을 추정하지 않으므로
호스트 어댑터가 제공자의 출처와 접근 권한을 검증해야 한다. 문자열은 유효한
UTF-8이어야 하고 NUL을 포함할 수 없다. 한 객체의 텍스트·메타데이터 바이트 합계는
최대 256 KiB, 구조 깊이는 32, 값 노드는 16,384개이며 평점은 0–5이다.

## 검색과 필터

텍스트는 iiFileProvider `FileSearchIndex`의 Unicode 단어 접두어 검색을 사용하고
공백으로 나눈 단어를 AND로 결합한다. 식별자, 제목, 경로, MIME, 설명, 키워드,
컬렉션, 사용자 필드와 제공된 표시 속성이 검색 대상이다. 검색은 미디어 원본
페이로드를 읽지 않는다. 패키지 유효성 재검사에는 제공자의 매니페스트·분류
정보 읽기가 포함될 수 있다. 표시 정보는 제공자가 검증한 상세·프리뷰 종류만 저장한다.

`ObjectFilter`는 텍스트 외에 세션 ID·제공자 ID·콘텐츠 버전·리비전·경로 접두사,
타입, MIME·확장자, 평점·크기·수정 시각 범위, 전체/하나 이상 키워드, 컬렉션, 색상, 분류 필드, 표시 속성,
분류/표시 유무와 발행 대기 상태를 조합한다. 조건들은 AND이며 같은 목록의
선택지는 OR이다. `allKeywords`는 예외적으로 모든 항목이 필요하다. 구조화된
키워드·컬렉션·필드 비교는 대소문자와 Unicode 표기를 포함한 정확한 값
비교이며 MIME·확장자·색상만 ASCII 대소문자를 구분하지 않는다. 경로 접두사는
전체 경로 또는 디렉터리 경계와 일치한다.

결과는 경로/ID의 바이트 순서로 최대 1,024개 반환한다. 내부에서는 제공자
색인을 페이지 단위로 읽고 필터를 먼저 적용하므로 앞의 1,024개가 불일치여도
뒤의 결과를 찾는다. 공개 결과 페이지 커서는 아직 제공하지 않는다. 빈 텍스트는
전체 스냅샷을 대상으로 하며, 기존 `Index::search()`의 순위 정렬과는 다르다.

iiFileProvider에 등록된 `.iiproject`는 `ObjectType::package` 한 개로
노출하고 내부 파일은 따로 색인하지 않는다. 유효한 분류 동반 파일과 함께
옮긴 독립 패키지도 같은 타입으로 표시한다. 카탈로그 패키지는 조회할 때
제공자의 키와 콘텐츠 버전으로 다시 확인한다. 독립 패키지는 유효한 매니페스트와
분류 동반 파일을 다시 확인하지만 제공자의 콘텐츠 버전이 없으므로 세션 생성
이후의 모든 내용 변경을 감지한다고 보장하지 않는다.

`iiGlobalSearch.session`은 직접 공급된 값, ID와 다른 상대 경로의 텍스트 검색,
조합 필터, 범위 오류, 중복 ID,
1,100개 항목 뒤의 일치, 동시 조회와 명시적 파괴를 검사한다.
`iiGlobalSearch.index`는 실제 제공자 카탈로그·표시 상세·발행 대기 메타데이터,
새로 고침과 색인 객체 소멸 후의 세션 수명, 패키지 경계 및 삭제된 원본의 재검사를 확인한다.
