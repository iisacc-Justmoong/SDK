# 독립 작업별 웹 탐색과 DOM 검색

## 범위와 시작점

`WebSearch`는 각 요청마다 `iiNetwork`로 HTTP(S) 문서를 새로 가져오고 링크를 따라가는 비동기 작업을 제출한다. 기존 Society 파일 색인과 별도 기능이다. 외부 검색 엔진·데이터베이스를 호출하지 않으며 검색용 영속 인덱스도 만들지 않는다. 방문 중복 집합, robots 규칙과 검색 결과는 해당 작업의 메모리에만 존재한다. 같은 요청을 다시 제출해도 네트워크와 robots를 다시 확인한다.

`WebConfiguration::defaultSeeds`는 호스트가 지정하는 기본 시작 URL이다. 요청의 `seeds`가 비어 있으면 이를 사용하고, 요청별 URL을 지정하면 기본값을 대체한다. `includeDefaultSeeds`를 켜면 둘을 합친다. CLI는 [기본 시작점 목록](../resources/web-seeds.txt)을 읽으며 `--seed`를 여러 번 주거나 `--seed-file`로 교체할 수 있다. 기본 목록은 최초 탐색을 위한 소수의 공용 페이지일 뿐 웹 전체를 대표하거나 특정 검색어에 대한 포괄적 결과를 보장하지 않는다.

큐는 너비 우선으로 링크를 따라가며 URL의 fragment를 제거하고 중복 방문을 줄인다. 페이지 수, 깊이, 결과 수, 응답 크기, DOM 노드 수, 요청 시간과 origin 간 요청 간격이 제한된다. 기본값은 페이지 20개, 깊이 2, 결과 20개, HTML 2 MiB, DOM 30,000개, 요청 및 렌더링 각각 20초, 같은 origin 요청 간 100ms이다. robots 요청과 브라우저의 CSS·이미지·스크립트 등 하위 요청은 페이지 수에 포함되지 않는다. `sameOriginOnly`로 발견 링크의 범위를 명시적으로 좁힐 수 있다.

robots.txt의 user-agent 그룹, 가장 구체적인 Allow/Disallow, `*`와 끝의 `$`를 처리한다. 최대 5번의 robots 리디렉션을 따라가며 요청 오류, 401·403·429·5xx 또는 미해결 리디렉션에서는 해당 origin을 탐색하지 않는다. 다른 4xx는 규칙이 없는 것으로 처리한다. HTML robots 메타 태그와 `X-Robots-Tag`의 `noindex`는 결과에서 제외하고 `nofollow`는 링크 추적을 중지한다. 링크의 `rel=nofollow`, `ugc`, `sponsored`도 추적하지 않는다. HTTP와 브라우저 내 JavaScript가 변경한 최종 URL은 새 큐 항목으로 다루어 robots를 다시 확인한다.

검색어의 공백 구분 항목이 제목·설명·텍스트·URL·DOM 요소명과 속성·HTTP 헤더·메타 태그·JSON-LD 중 어느 곳에든 모두 등장해야 한다. DOM을 출력하지 않는 요청도 요소명과 속성을 검색한다. 제목 등에 가중치를 주는 간단한 점수와 URL 순서로 정렬한다. ASCII 대소문자는 구분하지 않으며 ASCII 이외의 대소문자 접기·형태소 분석은 제공하지 않는다. Google의 링크 발견·크롤링·문서 분석이라는 원리를 참고하지만 Google의 저장 인덱스, 전체 웹 범위 또는 검색 순위를 복제하는 기능은 아니다. [Google의 검색 동작 설명](https://developers.google.com/search/docs/fundamentals/how-search-works), [JavaScript 처리 설명](https://developers.google.com/search/docs/crawling-indexing/javascript/javascript-seo-basics)을 참고한다.

## DOM과 렌더링

기본값으로 Chromium을 별도 프로세스와 임시 프로필에서 실행한다. iiSDKCore가 프로세스 수명·취소를 소유하고 iiNetwork의 로컬 DevTools 연결로 페이지 로드 후 DOM과 `location.href`를 받는다. 기본 사용자 브라우저의 프로필·로그인·쿠키를 사용하지 않는다. 프로필은 지정한 `scratchDirectory`의 임시 폴더에 만들고 정상 종료·취소 시 제거한다. 프로세스가 비정상 종료되면 임시 프로필이 남을 수 있다. 브라우저는 페이지의 스크립트와 하위 리소스를 요청할 수 있다.

`renderWait`은 페이지 로드 이벤트 이후 동적 변경을 기다리는 시간이다. 장시간 연결이나 더 늦게 표시되는 콘텐츠는 이 시점에 없을 수 있다. 브라우저 실행·렌더링이 실패하면 오류와 `skipped`를 기록하고 서버의 HTML 결과로 계속 탐색한다. 그 결과의 `rendered`는 `false`이다. 호스트는 `WebRenderer`를 구현하여 렌더러를 교체할 수 있으며 최종 URL을 반드시 반환해야 한다. 로컬 작업을 보호하기 위해 명시된 시작 origin을 제외한 localhost·사설 IP 리터럴 링크는 큐에 추가하지 않는다. 이 검사는 DNS 주소 고정이나 브라우저 하위 요청 차단을 제공하지 않으므로 신뢰할 수 없는 웹을 다룰 때의 완전한 네트워크 격리 경계는 아니다.

libxml2의 HTML 파서로 응답 또는 렌더링된 HTML을 파싱한다. 결과에는 최종 URL·HTTP 상태와 헤더, 제목·설명, 반복 메타 태그, canonical, JSON-LD, 링크, 가시 텍스트와 UTF-8 HTML이 들어간다. DOM은 순서가 있는 노드 배열이며 `parent=-1`은 루트, 나머지는 같은 배열의 부모 인덱스이다. 노드는 element/text/comment와 속성을 보존한다. `domTruncated`는 지정한 노드 또는 파서 순회 한도에 도달했음을 뜻한다. HTML5 브라우저의 모든 오류 복구 규칙을 복제하지 않는다. 미디어 참조와 HTML 표를 구조화 답변에 넣으며 미디어 본문은 별도 요청으로 비동기 적재한다. 외부 문서와 PDF 본문의 파싱은 제공하지 않는다. 자세한 타입과 범위는 [메모리 내 구조화 검색 답변](Answers.md)을 따른다.

## API와 CLI

웹만 사용하는 소비자는 `find_package(iiGlobalSearch CONFIG REQUIRED COMPONENTS Web)`과 `iiGlobalSearch::Web`을 사용한다. 로컬 파일 검색을 함께 사용하면 `COMPONENTS Local Web`과 두 타깃을 요청한다. 웹 소비자가 Society 파일 검색 타깃을 링크할 필요는 없다.

```cpp
#include <WebSearch.h>
#include <chrono>
using namespace std::chrono_literals;

iiGlobalSearch::WebConfiguration config;
config.defaultSeeds = {"https://example.com/"};
config.scratchDirectory = "/absolute/workspace/resources/web-rendering";
iiGlobalSearch::WebSearch search(config);
iiGlobalSearch::WebRequest request;
request.query = "example domain";
request.maxPages = 20;
auto task = search.submit(request); // 큐가 가득 찼으면 빈 포인터
if (task && task->waitFor(30s)) {
    auto state = task->progress();
    auto pages = task->results(); // 기존 호환 API
    auto answer = task->answer(); // 메모리 내 구조화 객체
}
// task->cancel()은 활성 네트워크와 브라우저 작업을 종료한다.
```

`WebSearchProvider`와 `WebTask`는 대체 구현이 가능한 인터페이스이다. `WebFetcherFactory`는 각 작업에 독립된 전송 객체를 제공하며 생략하면 iiNetwork를 사용한다. `WebRenderer`와 전송 팩토리는 여러 작업자에서 동시에 호출될 수 있으므로 주입하는 구현은 그 수명과 동시성을 지켜야 한다. `WebSearch`는 기본적으로 재사용 작업자 2개와 큐 용량 16개를 사용한다. 상태는 `queued`, `running`, `completed`, `cancelled`, `failed`이며 검색 중에도 결과를 복사해 읽을 수 있다. `completed`여도 `skipped`와 최대 32개 `diagnostics`를 확인해야 한다. 이는 차단·오류나 렌더링 대체가 있었음을 뜻한다. `truncated`는 깊이·페이지·탐색 큐·결과 수 제한으로 범위가 잘렸음을 뜻한다. 소멸은 미완료 작업을 취소하고 작업자가 끝날 때까지 기다린다.

```sh
build/ii-global-search web "creative tools" --seed https://example.com/ --max-pages 10 --depth 2
build/ii-global-search web "search terms" --seed-file resources/web-seeds.txt --limit 10
build/ii-global-search web "search terms" --seed https://example.com/ --raw --same-origin
```

`--include-default-seeds`, `--browser`, `--scratch`, `--timeout-ms`, `--render-wait-ms`, `--delay-ms`, `--no-dom`을 사용할 수 있다. `--raw`는 JavaScript 실행을 생략한다. CLI는 작업 종료 후 JSON을 출력한다. UTF-8이 아닌 응답 문자열은 JSON 출력에서 바이트별 Latin-1로 변환하며 C++ API의 원래 헤더 바이트는 보존한다. 소스 트리 실행 또는 설치된 `share/iiGlobalSearch/web-seeds.txt`를 찾지 못하면 `--seed`, `--seed-file` 또는 `IIGLOBALSEARCH_SEED_FILE`이 필요하다.

`iiGlobalSearch.web`은 교체 가능한 가짜 전송·렌더러를 사용하여 DOM, 메타데이터·JSON-LD, 상대 링크, robots·noindex, HTTP/렌더러 리디렉션, URL 인코딩, 제한, 취소 및 재검색을 검사한다. CLI 인자 해석과 응답 JSON도 앱을 실행하지 않고 검사한다. `iiGlobalSearch.web.integration`은 실제 로컬 HTTP 서버·격리된 Chromium·CLI를 사용하는 별도 검사이며 기본 CTest에서는 비활성화되어 있다. 사용자 요청이 없는 실제 앱·브라우저 실행 검증을 피한다. 실제 공용 사이트와 다른 운영체제의 실행은 별도의 증거가 필요하다.
