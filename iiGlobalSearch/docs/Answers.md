# 메모리 내 구조화 검색 답변

## 답변 모델과 소유권

`Index::searchAnswer(query, limit)`와 `WebTask::answer()`는 `Answer`를 반환한다. 이는 최상위가 객체인 재귀적 `Value`이며 결과 파일을 만들거나 답변을 디스크에 저장하지 않는다. `source`, `state`, `partial`, `results`, 오류·제한 정보가 들어간다. 각 결과의 속성은 발견된 자료에 따라 달라진다. 기존 `search()`와 `results()`도 소스 호환성 때문에 유지한다. 공개 구조체와 가상 인터페이스가 바뀌었으므로 기존 C++ 바이너리 소비자는 다시 빌드해야 한다. 별도 구현을 사용하는 경우 `SearchProvider::searchAnswer()`와 `WebTask::answer()`를 재정의할 수 있다. 기본 구현은 기존 결과에서 답변을 생성하며 로컬 대체 구현에는 자동으로 컨테이너 파일 참조를 부여하지 않는다.

`Value::get<T>()`는 정확한 타입의 읽기 전용 포인터를 반환하고, 객체의 `find(key)`는 없거나 객체가 아니면 빈 포인터를 반환한다. `Value::List`는 서로 다른 `Value`를 재귀적으로 담고, `Value::Object`는 문자열 키를 가진다. 숫자·문자열 외에도 null, bool, 바이트 배열을 보존한다. 양수 정수는 부호 있는 `int64_t` 또는 부호 없는 `uint64_t`로 정확히 보존하며 `double`은 Float이다. 분류 JSON과 JSON-LD는 iiSDKCore `Value`와 iiJson을 통해 구조를 보존하고 이 답변 값으로 변환한다. 유효하지 않은 JSON-LD는 원래 문자열로 둔다. 입력이 없는 유형을 추측하여 생성하지 않는다.

| 유형 | 답변 표현과 원본 |
| --- | --- |
| Int, Float, String | 각각 `int64_t`/`uint64_t`, `double`, `std::string`이다. |
| List[Any], Object | `Value::List`, `Value::Object`이다. 모든 유형을 중첩할 수 있다. |
| Image, Video, Audio | 각 `content`에 MIME 힌트와 출처, 선택적으로 메모리 바이트가 있다. |
| Vector | `std::vector<double>`인 숫자 벡터 또는 인코딩된 벡터 그래픽 `Asset`이다. |
| Spreadsheet | HTML 표의 행과 `ii::core::Value` 셀 또는 외부 문서의 `source` 참조이다. |
| Graph | 노드·간선 및 수치 좌표를 가진 데이터 계열과 속성이다. 요청된 웹 DOM은 `domGraph`로도 표현된다. |
| Scene | 노드·부모·행렬·속성 또는 3D 문서의 `source` 참조이다. |

로컬 답변의 항목은 상대 경로, 파일 키, 제목, 크기, MIME, 분류 객체 및 존재·대기 상태를 포함한다. 검색된 파일의 MIME이나 확장자로 미디어, 벡터 그래픽, 스프레드시트, 3D 장면을 식별하면 `value`에 해당 타입의 참조를 담는다. 판별은 형식 힌트이며 파일을 디코딩하거나 MIME을 인증하지 않는다. 분류되지 않은 일반 파일이 새로 색인 대상이 되지는 않는다.

웹 답변은 페이지의 DOM 노드와 속성, 반복 가능한 메타 태그·HTTP 헤더, 텍스트·HTML·링크·JSON-LD, 점수와 렌더링 상태를 보존한다. HTML의 `img`, `video`, `audio`, 이들의 `source`, Open Graph/Twitter 미디어 메타데이터와 알려진 미디어·문서 링크에서 타입이 있는 참조를 찾는다. HTML `table`은 실제 행과 셀을 담은 Spreadsheet이며 인라인 SVG는 이미 받은 문서 안의 벡터 그래픽 바이트이다. 표와 참조는 DOM 출력을 끈 경우에도 추출한다. 최대 1,024개 추출 항목, 표 4,096개 셀, 4 MiB 인라인 SVG 한도를 두며 제한에 걸리면 `assetsTruncated`가 참이다. DOM 자체는 별도 한도를 따른다. 파일명과 태그만으로는 해당 리소스의 진짜 형식이나 접근 가능성을 보장할 수 없다.

Spreadsheet 파일과 Scene 파일은 소스 참조로 표시되며 자동으로 표·3D 장면으로 파싱되지 않는다. 숫자 벡터, 사용자 정의 그래프·장면은 소비자나 교체 가능한 변환기가 직접 `Value`에 넣을 수 있다. 이미지·영상·음성 및 벡터 그래픽의 적재 결과는 **인코딩된 원본 바이트**이다. 픽셀, 프레임, 샘플, 장면 디코딩은 iiVisualCodec 등 적합한 상위 소비자가 별도로 수행한다. 이 계층은 특정 코덱이나 문서 모듈에 결합하지 않는다.

## 필요 시 비동기 미디어 적재

`Asset::bytes == nullptr`는 미적재이고, 길이가 0인 유효한 버퍼는 적재된 빈 파일이다. `AssetReference`는 컨테이너 상대 경로나 웹 URL과 출처 및 MIME 힌트를 나타내며 payload를 포함하지 않는다. `MediaLoader`는 iiSDKCore의 제한된 재사용 작업자 풀을 사용한다. `submit(value, maximumBytes)`는 즉시 작업을 반환하고, 큐가 가득 차면 빈 포인터를 반환한다. 잘못된 타입, 이미 적재된 항목, 0 또는 1 GiB 초과 제한은 예외이다. 기본 제한은 작업당 32 MiB이다. 검색 결과는 수정하지 않고 `LoadTask::result()`에서 바이트를 공유하는 별도의 값 스냅샷을 얻는다. 로더는 작업 전체의 누적 메모리 한도를 제공하지 않으므로 호출자가 보유한 답변과 로드 동시성을 관리해야 한다.

```cpp
#include <GlobalSearch.h>
#include <chrono>
using namespace std::chrono_literals;

iiGlobalSearch::Index index("/absolute/Library.societycontainer");
index.start();
index.waitFor(30s);
auto answer = index.searchAnswer("portrait");
const auto* resultsValue = answer.find("results");
const auto* results = resultsValue ? resultsValue->get<iiGlobalSearch::Value::List>() : nullptr;
if (results && !results->empty()) {
    const auto* image = results->front().find("value");
    if (image && image->get<iiGlobalSearch::Image>()) {
        iiGlobalSearch::MediaLoader loader(iiGlobalSearch::makeLocalAssetReader("/absolute/Library.societycontainer"));
        auto task = loader.submit(*image, 16 * 1024 * 1024);
        if (task && task->waitFor(30s) && task->progress().state == iiGlobalSearch::LoadState::completed) {
            auto loaded = task->result(); // 별도 메모리 객체. 파일을 만들지 않는다.
        }
    }
}
```

`makeLocalAssetReader`는 선택한 정상 컨테이너의 디렉터리 핸들을 고정한다. 각 상대 경로의 숨김 요소, `..`, 링크·리파스 포인트를 거부하고 핸들 기준으로 파일을 연다. POSIX에서는 iiFileProvider의 `openFileAt`과 `O_NOFOLLOW`, Windows에서는 `createFileNt`의 상대 핸들과 리파스 포인트 검사를 사용한다. 읽기 전후에 비교한 파일 식별자·크기·수정 시간이 다르면 실패한다. 검색 이후 읽기 전에 원본이 바뀌는 것은 막지 못하므로 참조는 현재 파일에 대한 참조이지 과거 파일의 내용 보증이 아니다. Windows 구현은 별도의 Windows 실행 검증이 필요하다.

웹 값은 `MediaLoader(makeWebAssetReader(config.fetchers))`로 적재한다. 매 작업마다 새 전송 객체를 만들며 robots와 HTTP(S), 리디렉션 및 바이트 제한을 다시 검사한다. 검색 페이지와 다른 localhost·사설 IP 리터럴은 차단하지만 DNS 고정이나 완전한 네트워크 격리는 제공하지 않는다. 주입한 `WebFetcherFactory`와 `AssetReader`는 여러 작업자에 안전해야 하며 취소 토큰·크기 제한을 준수해야 한다. 독자 구현이 한도를 넘겨 반환해도 로더가 다시 거부한다. 브라우저로 JavaScript를 렌더링하는 동안 브라우저 자체는 이미지 등 하위 리소스를 요청할 수 있지만, 결과 객체의 원격 미디어 바이트는 별도 적재 전까지 비어 있다. 웹 페이지와 미디어는 검색 이후 변경될 수 있다.

작업 상태는 `queued`, `running`, `completed`, `cancelled`, `failed`이다. `cancel()`은 협력적이며 `waitFor()`로 종료를 확인한다. 로더 소멸은 미완료 작업을 취소하고 작업자가 종료될 때까지 기다린다. 독자적인 읽기 구현 안에서 로더를 소멸하지 않는다. 인라인 SVG처럼 이미 바이트가 있는 값은 다시 제출하지 않는다. 데이터의 영속 저장은 호출자가 별도로 선택하는 행위이며 이 API가 수행하지 않는다.

로컬 결과 조회와 진행 상태는 연속된 별도 관찰이므로 색인 중에는 서로 정확한 원자적 스냅샷이 아닐 수 있다. 기본 WebSearch의 `answer()`는 결과와 상태를 한 번에 복사한다. 대체 WebTask의 기본 구현은 별도 관찰이다.

## 구성과 검사

공통 값과 로더만 필요하면 CMake에서 `find_package(iiGlobalSearch CONFIG REQUIRED COMPONENTS Answer)`와 `iiGlobalSearch::Answer`를 사용한다. 로컬 또는 웹 타깃은 이 공통 타깃을 전이적으로 제공하며 웹만 사용할 때 Society나 SQLite 검색 타깃이 필요하지 않다.

`iiGlobalSearch.answer`는 이종 재귀 값, 비동기 제출, 불변 결과 스냅샷, 취소와 크기 제한을 가짜 리더로 검사한다. `iiGlobalSearch.index`는 실제 합성 컨테이너에서 메타데이터 및 이미지 참조, 지연 적재와 경로 이탈 차단을 검사한다. `iiGlobalSearch.web`은 가짜 네트워크·렌더러로 동적 DOM, 미디어·표·그래프·장면 참조, 지연 적재, robots와 사설 origin을 검사한다. 실제 브라우저, 공용 인터넷, Windows와 Linux 실행 결과를 이 테스트에서 보장하지 않는다.

Windows 핸들 기준 경로 열기와 리파스 포인트 동작은 [Microsoft의 NtCreateFile 설명](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntcreatefile)을 참고한다.
