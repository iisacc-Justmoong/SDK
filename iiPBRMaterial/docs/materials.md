# 물성, 분자식, 텍스처와 체적 연결

## 책임과 단위

`iiPBRMaterial`은 물리 소재 레코드와 표면 PBR 표현을 보유한다. 밀도는
kg/m³, 온도는 K, 압력·탄성률·항복 응력은 Pa이다. 공식과 물성의 원본 ID,
밀도 출처 및 알려진 기준 조건을 보존한다. 밀도는 수식이나 색에서 추측하지
않고 검증 가능한 레코드에서 가져온다. Qt, 파일 시스템 및 다른 iiSDK 모듈에
의존하지 않으며 앱이 저장·파일 제공, 디코더와 다른 SDK를 따로 조합한다.

표면 텍스처의 기본 색·금속도·거칠기는 빛과 표면의 상호작용을 위한 값이다.
이는 내부 밀도나 화학 성분을 결정하지 않는다. 이 구분은
[glTF PBR 재질 정의](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials)와
일치한다. 임의의 사진이나 PBR 색상에서 화학식을 인식하는 AI 기능은 없다.

## 분자식과 물성 조회

`parseMolecularFormula()`는 원소 기호, ASCII 또는 Unicode 아래첨자 정수 원자 수, 중첩 `()`·`[]`, 점
또는 가운데점으로 구분하는 수화물·부가물 항을 해석한다. 예를 들어
`CuSO4·5H2O`는 Cu 1, S 1, O 9, H 10을 반환한다. 원자 구성을 Hill 순서로
정규화하되 비율을 약분하지 않는다. 동위원소, 전하, 분수 화학량론,
고분자 표기 및 첫 항의 계수는 허용하지 않는다. 입력 길이·중첩·원자 수는
제한하며 잘못된 식을 부분적으로 받아들이지 않는다.

사용자 정의 원자는 `{ID}`로 표현하며 별도의 `CustomAtomType` 정의를 전달했을
때만 인식한다. 이는 SDK 전용 표기다. 몰 질량은 등록된 사용자 질량과 알려진
기존 원소의 원자량을 합산하며, 하나라도 알 수 없으면 결과를 비운다.
원자 몰 질량은 벌크 소재의 밀도나 체적 솔리드의 질량을 대신하지 않는다.
[사용자 정의 원자와 예제](molecular-map.md#사용자-정의-원자)를 참고한다.

대표적인 원소의 반올림한 자연 동위원소 평균 원자량만 내장한다. 모든
원소의 원자량이 있을 때만 `molarMassGramsPerMol`을 채우며, 빠진 원소가
있으면 값을 비워 둔다. 이는 물체 질량이나 밀도가 아니다. 원자량의 기준은
[NIST 원자량 표](https://physics.nist.gov/cgi-bin/Compositions/stand_alone.pl?all=all&ascii=html)를 참고한다.

`MaterialCatalog::resolve(formula, materialId)`는 정규화한 분자식과 명시된
소재 ID가 둘 다 일치하는 물성 레코드를 반환한다. ID를 생략하면 등록된
카탈로그에 일치 항목이 하나일 때만 반환하고, 여러 항목이면 `ambiguous`
오류를 낸다. `candidates()`로 선택지를 확인할 수 있다. 단일 결과라는 것은
**현재 카탈로그에 하나가 있다는 의미**이며 실제 물질이 유일하다는 증거가
아니다. 같은 분자식은 상·결정 구조·이성질체 등에 따라 서로 다른 물질과
밀도를 가질 수 있다. 앱은 가능한 한 명시적인 ID와 필요한 기준 조건을
확인해야 한다. 없는 물성은 오류이며 임의로 예측하지 않는다.

내장 카탈로그는 제한된 예시로 Fe 7874, Al 2699, Cu 8960, Si 2330,
흑연 C 1700 kg/m³를 포함한다. 이는 [NIST 원소 물성 표](https://physics.nist.gov/PhysRefData/XrayMassCoef/tab1.html)의
명목 밀도이며 해당 표가 명시하지 않은 온도·압력은 비워 둔다. 액체 H₂O는
20°C에서 998.20675 kg/m³인 [NIST 물 표](https://www.nist.gov/file/40736)를
참조하며 온도는 293.15 K로 기록한다. 이 값들을 임의의 합금, 다공성 재료,
임의 온도·압력 또는 정밀 공학 해석의 확정값으로 가정하면 안 된다.
앱은 `add()`로 출처와 양의 밀도를 포함한 별도의 레코드를 등록할 수 있다.
합금·혼합물처럼 하나의 분자식으로 나타낼 수 없는 소재는 공식을 비워 둔
레코드로 등록하고 `resolveId()`로 선택한다. 이 경우 분자식 결과도 비어 있으며,
밀도는 그 소재 자체의 측정값이나 신뢰할 수 있는 자료에서 공급해야 한다.

사용자 원자가 포함된 소재는 `MaterialCatalog::add(record, customTypes)`로
등록한다. 식 문자열로 조회할 때는 `resolve(formula, customTypes, id)`에 같은
정의를 전달한다. `resolve(molecule, id)`와 `resolveIsomer()`는 분자에 보유된
정의를 사용한다. 다른 정의에 같은 ID가 재사용되면 오류가 발생한다. 유기
소재의 벌크 식에 사용자 원자를 넣을 때는 `OrganicMaterialDefinition`의
`customAtomTypes`에 정의를 보관한다. 성분의 분자 객체에만 사용자 원자가 있으면
그 객체가 보유한 정의를 성분 식 검증에 사용한다.

## 유기물과 생체 소재

`OrganicMaterial`은 유기 화합물, 생체 조직 또는 사용자가 설계한 생물의
**소재 정의**와 시간별 **상태**를 분리한 C++23 값 객체이다. 종이나 소재별
내장 수치를 가정하지 않는다. 정의에는 기존 `MaterialRecord`와 PBR 표면,
선택적인 광학 계수, 성분 계층, 성분 간 관계 및 사용자 정의 속성을 넣는다.
물, 단백질, 지질, 세포, 세포외기질, 조직, 기관이나 개체 등 성분 종류는
고정 열거형이 아니라 앱이 정하는 문자열이므로 새로운 유형도 표현할 수 있다.

각 `OrganicComponent`는 고유 ID·이름·유형·부모 ID를 갖는다. 부모 관계는
비순환 구성 트리이며, `OrganicRelation`으로 신경 연결이나 결합 등의 별도
방향 관계를 추가할 수 있다. 관계 그래프에는 순환이 가능하다. `Molecule`이나
명시적 분자식을 성분에 보관할 수 있고 둘 다 제공하면 원소별 개수를
검사한다. 선택한 이성질체의 원자·결합 그래프도 `Molecule`으로 넣을 수 있다.
구조가 특정 소재의 실제 화학적 정체성이나 안정성에 해당하는지는 검증하지
않는다. 거대 분자와 혼합물에는 분자식을 생략하고 서열·외부 데이터의 식별자
등을 속성으로 지정할 수 있다.

생성된 정의는 불변이다. 앱은 `definition()`의 복사본을 편집해 새
`OrganicMaterial`을 만들 수 있으며, 새 정의에는 그 정의에 속하는 상태를
새로 만들어야 한다. 기존 정의와 상태는 유지된다.

질량·체적·몰 분율은 각각 선택 사항이며 **직계 부모에 대한 분율**이다.
각 기준의 알려진 형제 분율 합계는 1을 넘을 수 없다(부동소수점 허용 오차
`1e-9`). 생략은 미상이며 0으로 채우거나 합계를 1로 정규화하지 않는다.
이 분율은 자료 기록이며 혼합물 밀도를 자동 추정하거나 체적 메시의 공간
경계를 자동 생성하지 않는다.

`OrganicProperty`는 사용자 키와 bool, 64비트 정수, 실수, 문자열, 정수·실수·
문자열 배열 및 단위·출처 문자열을 담는다. 소재, 성분, 관계, 상태, 성분별
상태에 각각 부여할 수 있다. 예컨대 섬유 방향과 점탄성 계수 배열, 조직
분류, 발현량, 유전자 서열의 외부 ID, 성장 조건이나 사용자 모델 매개변수를
넣을 수 있다. 키별 단위 변환이나 값의 생물학적 의미는 앱과 해당 모델의
계약이다. 같은 범위에서 중복 키, 비유한 실수와 잘못된 참조는 거부한다.
정의는 최대 성분 4,096개, 관계 16,384개, 각 속성 범위당 4,096개를 허용한다.
배열은 최대 1,000,000개 항목, 문자열은 각각 최대 4 MiB이며 큰 원본 데이터는
앱이 별도 자원으로 보유하고 식별자를 전달한다.

### 상태, 동역학과 물성

`OrganicStateData`에는 시간, 환경 온도·압력·산소 분압·pH, 수분 질량 분율,
생존 분율, 선택적인 밀도·탄성 물성 및 임의 속성을 지정한다. 상태는 생성한
`OrganicMaterial` 정의에 속하며 다른 정의의 상태와 혼용할 수 없다.
밀도를 상태에서 바꾸려면 양의 밀도와 그 출처가 필요하다. 이때 밀도의 기준
온도·압력은 별도로 명시하지 않으면 미상이다. 환경 온도를 입력했다는 이유로
밀도가 그 온도에서 측정됐다고 간주하지 않는다. 밀도 변경이 없으면 기존
레코드의 기준 조건을 보존한다.

`OrganicStateModel::step()`은 앱이 교체 가능한 모델을 제공하도록 하는
인터페이스다. `advance()`는 복사한 상태를 모델에 전달하고 유효성 검사에
성공할 때만 새 상태를 반환한다. 실패·예외 시 원래 상태는 바뀌지 않으며
시간은 SDK가 지정한 초만큼 전진시킨다. 모델의 외부 부작용은 롤백하지
않는다. 내장 `LogisticPopulationModel`은 사용자가 정한 개체군 속성, 양의
수용 한계와 0 이상의 초당 성장률로 로지스틱 개체군 근사를 계산한다.
이는 단일 집단의 단순 모델이며 세포 분열·대사·면역이나 개체 생존을
자동으로 시뮬레이션하지 않는다. 개체 수나 수분 변화로 밀도와 질량을 자동
계산하지도 않는다. 필요한 관계는 앱의 검증된 모델이 명시적으로 제공한다.

```cpp
#include <OrganicMaterial.h>
using namespace ii::pbr;

OrganicMaterialDefinition definition;
// 아래 수치와 출처는 동작을 보이는 테스트 픽스처다.
definition.physical = {"example:tissue", "Synthetic tissue", "", "tissue",
                       1050, {}, {}, 12000, 0.4, 3000, "example fixture"};
OrganicComponent cells;
cells.id = "cells"; cells.name = "Cells"; cells.kind = "cell";
cells.properties.push_back({"fiber_direction", std::vector<double>{1,0,0}, "", "example"});
definition.components.push_back(cells);

auto material = OrganicMaterial::create(std::move(definition));
if (material) {
    OrganicStateData input;
    input.temperatureKelvin = 310; input.hydrationMassFraction = 0.7;
    input.components.push_back({"cells", {{"population", 1000.0, "count", "example"}}});
    auto state = material->makeState(input);
    auto model = LogisticPopulationModel::create({"cells", "population", 0.01, 10000});
    if (state && model) {
        auto next = material->advance(*state, *model, 1.0);
        // next의 오류를 확인한 뒤 앱의 솔리드와 연결한다.
    }
}
```

### 유기 소재의 광학 표현

`OrganicOpticalProfile`은 RGB별 흡수·산란 계수(1/m), 산란 비등방성
`[-1,1]`, 굴절률을 보유한다. `uncollidedTransmittance(profile, distance)`는
균일 매질에서 산란되지 않은 직진광의 비율 `exp(-(흡수+산란) × 거리)`를
각 채널에 대해 계산한다. 산란 계수에는 감소 산란 계수 `μs'`가 아니라
전체 산란 계수 `μs`를 전달한다. [Khronos의 체적 감쇠 설명](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_volume/README.md)과
같은 감쇠 계수 관계를 사용하되, 이는 전체 산란광이나 피부의 subsurface
scattering을 렌더링하는 기능이 아니다. 굴절률과 비등방성은 보관하지만 이
직진광 계산에 적용하지 않는다. RGB 계수는 전체 파장별 스펙트럼을 대신하지
않으며 실제 렌더러와 광학 데이터는 앱이 제공한다.

## 이미지와 소재 ID 텍스처

SDK는 이미지 파일을 열지 않는다. 앱이 iiFileProvider 등으로 획득한 바이트를
`TextureDecoder`에 넘기거나, 디코딩한 RGBA8 픽셀을 `Texture::create()`에
넘긴다. 내장 `PpmDecoder`는 8비트 P6 PPM을 메모리에서 디코딩한다. PNG 등은
앱의 디코더를 주입하며, [iiVisualCodec 조합 어댑터](../../iiVolumetricSolidMesh/example/PbrMaterial/VisualTextureDecoder.cpp)는
PNG·JPEG·WebP·TIFF·BMP·TGA를 메모리에서 디코딩한다. iiPBRMaterial 자체는
이 모듈에 링크하지 않는다.

`MaterialIdMap`은 이미지의 RGB 색상과 카탈로그 ID를 명시적으로 연결한다.
소재 선택은 불투명 픽셀과 정확한 색상 일치를 요구하며 최근접 샘플링한다.
중간 색 보간·색상으로부터의 화학 추론은 하지 않는다. `v=0`은 이미지
위쪽이고 주소 모드는 clamp 또는 repeat이다. 소재 ID 맵에는 PNG처럼 색이
정확히 보존되는 포맷을 사용해야 한다. JPEG 손실 압축, 색상 보정, 필터링,
밉맵 또는 투명 픽셀은 ID를 바꾸거나 모호하게 만들 수 있다.

`sampleSurface()`는 별도의 표면 모양을 샘플링한다. 기본 색 RGB는 sRGB에서
선형 색으로 변환하고 알파는 선형으로 적용한다. 금속도·거칠기 텍스처의 B/G
채널은 선형으로 읽으며 각 factor를 곱한다. 이 구현은 PBR 렌더러나 GPU
셰이더 자체는 아니다. 색상 프로파일 변환과 자산의 색공간 확인은 앱이
관리하며, 범용 디코더는 색상 프로파일을 자동 변환하지 않는다.

## iiVolumetricSolidMesh와 앱 조합

[MaterialComposition.cpp](../../iiVolumetricSolidMesh/example/PbrMaterial/MaterialComposition.cpp)는
앱 수준 어댑터이다. 앱이 선택한 `(소재 ID, 분자식, 체적 소재 ID, 영역 ID)`
바인딩을 물성 카탈로그에 확인하고 밀도, 탄성 물성, 가능한 경우 화학식과 출처를 체적
객체의 불변 스냅샷으로 복사한다. 체적 모듈이 iiPBRMaterial을 직접
참조하거나 링크하지는 않는다. 소재 데이터가 바뀌면 앱이 새 스냅샷과
객체를 만들어 질량을 다시 계산한다. 기존 설계와 시뮬레이션 상태는 유지된다.
앱은 각 바인딩에 표면 PBR 텍스처를 보유하고 `sampleAppearance()`로
표면 삼각형의 영역 ID와 UV에서 표시용 PBR 값을 얻을 수 있다. 소재 ID
텍스처는 공간 영역 선택용이며 표면 색·거칠기 텍스처와 별도로 관리한다.

### 선택한 이성질체의 소재 적용

앱은 `applyIsomerMaterial(solid, catalog, molecule, isomerIndex, binding)`으로
기존 솔리드의 한 영역에 소재를 적용할 수 있다. `binding.catalogId`는 필수이며,
`binding.molecularFormula`를 제공하면 선택한 후보와 원소별 개수가 같아야 한다.
선택한 후보 및 카탈로그 소재의 식도 비교한다. 아래는 앱이 이미 구성한
`solid`, `catalog`, `family`를 사용하는 예다.

```cpp
ii::material_app::Binding choice{"catalog:ether", "C2H6O", 3, 11};
auto applied = ii::material_app::applyIsomerMaterial(solid, catalog, family, 0, choice);
if (applied) {
    const auto& updatedSolid = applied->solid;
    const auto& selectedStructure = applied->isomer;
    auto mass = updatedSolid.referenceMassProperties().mass; // kg
    // 앱은 updatedSolid와 selectedStructure, regionId 및 표면 표현을 함께 보관한다.
}
```

앱이 지정한 소재의 밀도와 탄성률·포아송비·항복 응력, 출처, 기준 온도와
압력을 새 체적 소재 스냅샷에 복사한다. 기존 사면체와 입자 셀을 보존하면서
해당 영역의 입자 질량과 솔리드 전체의 기준 질량·질량중심·관성을 다시
계산한다. 다른 영역은 그대로 유지한다. 선택 결과는 분자 구조, 선택 인덱스,
영역 ID, 표면 표현과 새 솔리드를 함께 반환한다. 기존 솔리드와 그 시뮬레이션
상태는 바뀌지 않는다. 새 솔리드에는 새 시뮬레이션 상태가 필요하다.

같은 체적 소재 ID를 다른 영역에서 이미 쓰고 있다면 그 소재를 덮어쓰지 않도록
오류를 반환하므로, 그 영역에만 적용할 새 소재 ID를 지정한다. 존재하지 않는
영역, 인덱스 또는 소재 ID와 다른 분자식도 오류다. 후보의 조성과 카탈로그의
식만 일치하는지 확인하므로 **구조가 카탈로그의 소재와 실제로 같은 물질인지는
검증하지 않는다**. 앱은 물질의 정체성과 상·온도·압력에 맞는 레코드를
지정해야 한다. 솔리드는 선택된 분자 그래프를 직접 소유하지 않으므로 앱은
반환된 선택 구조와 솔리드의 연관 관계를 함께 보존·저장해야 한다.

유기 소재는 앱 어댑터의 `applyOrganicMaterial(solid, material, state,
materialId, regionId)`로 연결한다. 검증된 소재 상태의 밀도·선택적 탄성
물성과 표면을 지정한 영역에 복사하고, 기존 체적의 질량을 다시 계산한다.
새 솔리드, 원본 유기 소재 정의와 선택한 상태를 함께 반환하므로 앱은 이를
함께 보관해야 한다. 다른 영역이 이미 쓰는 소재 ID의 덮어쓰기와 다른 정의에
속한 상태는 거부한다. 모델 상태를 변경해도 솔리드는 자동으로 변경되지
않는다. 앱은 필요한 시점에 다시 적용하고 새 솔리드에 맞는 시뮬레이션
상태를 만들어야 한다.

`TextureMappedMesher`는 명시적인 평면 투영으로 각 사면체의 중심을 소재 ID
텍스처에서 샘플링해 해당 영역을 할당한다. 이는 한 사면체에 하나의 소재를
부여하는 근사이다. 같은 UV로 투영되는 깊이 방향은 같은 소재가 이어진다고
취급하며 표면 코팅의 두께를 추정하지 않는다. 텍스처 경계가 사면체 내부를
지나가면 정확한 경계가
아니며, 이산화 해상도에 따라 질량이 달라질 수 있다. 정확한 소재 경계가
필요하면 그 경계를 따르는 체적 분할기 또는 별도의 체적 소재 필드가
필요하다. 2D 표면 텍스처만으로 물체 내부의 3D 소재 구성을 알아낼 수 없다.
통합 테스트의 1 m³ 상자를 철과 알루미늄으로 반씩 나누면 경계와 일치하는
분할에서는 5286.5 kg이다. 경계를 가로지르는 거친 분할에서는 같은 중심
샘플링이 4424 kg을 반환하므로 해상도와 분할 방향을 검증해야 한다.

## 검증과 설치 소비자

단위 테스트는 화학식·중첩·수화물·잘못된 구문, 레코드 모호성, 밀도 조건,
PPM, 소재 팔레트, 색공간 및 금속도·거칠기 채널을 검사한다. 별도의
[앱 통합 테스트](../tests/Integration/Integration.cpp)는 설치 스테이징한 두
SDK와 iiVisualCodec을 앱에서 각각 링크하고 PNG 소재 ID 맵으로 철과
알루미늄 영역을 지정해 체적 질량을 확인한다. 같은 테스트는 합성 에탄올·에터
레코드와 선택된 구조를 사용해 한 영역의 소재·입자 질량·전체 질량을 바꾸고,
잘못된 입력 및 공유 소재 ID의 격리를 확인한다. 수치는 테스트 픽스처이며 실제
측정값이 아니다. 유기 소재의 성분·속성·관계·상태 검증, 교체 가능한 모델,
광 감쇠 및 체적 질량 반영도 검사한다. 해당 테스트의 CMake 프로젝트는
앱 테스트로서 세 SDK를 조합하며 SDK 모듈 간 의존성을 추가하지 않는다.
동일한 앱 조합 코드는 [독립 CMake 프로젝트](../../iiVolumetricSolidMesh/example/PbrMaterial/CMakeLists.txt)로도 제공한다.

현재 검증은 macOS에서 수행했다. 다른 플랫폼의 빌드와 기기 실행, 실제
소재 식별 모델 및 공학 해석 검증은 별도 범위이다.
