# 분자 객체와 분자 구조도

## 범위와 데이터 경계

기존 `MolecularFormula`는 원소별 개수와 표준 순서의 식을 표현한다. 식만으로
원자들이 어떻게 결합했는지, 구조 이성질체나 입체 이성질체가 무엇인지는
결정할 수 없다. `Molecule::create()`는 앱이 명시한 `Atom`과 `Bond` 목록에서
분자식 객체를 생성하고, 선택적으로 전달한 기대 분자식의 원자 수와 구조가
일치하는지 검사한다. 앱은 외부의 검증된 구조 데이터를 선택적으로 연결할 수
있지만 이 모듈은 다른 SDK나 특정 화학 데이터 서비스에 의존하지 않는다.

각 원자는 원소 기호, 선택적 동위원소 질량수, 형식 전하와 암묵적 수소 수를
가진다. 원자별 표시는 예를 들어 `[13]CH4-`와 같다. 식의 조성은 명시된
수소와 암묵적 수소를 모두 포함한다. 식의 `canonical`에는 전하와 동위원소를
인코딩하지 않으며, 동위원소를 지정한 구조에는 자연 동위원소 평균으로
계산한 몰 질량을 적용하지 않고 몰 질량을 비운다. 전체 형식 전하는
`totalFormalCharge()`로 확인한다.

## 사용자 정의 원자

`CustomAtomDefinition`은 사용자가 정한 원자 또는 가상 입자의 **고유 ID**,
화면에 표시할 기호와 이름을 갖는다. 양성자·중성자·전자 수, 몰 질량(g/mol),
공유결합 반지름과 반데르발스 반지름(m)을 각각 선택적으로 지정할 수 있다.
`CustomAtomProperty`로 임의의 숫자·문자열·배열과 단위·출처도 보관한다.
입력된 입자 수에서 질량이나 전하를 자동으로 계산하지 않으며, 해당 원자가
실제로 존재하거나 화학적으로 안정하다고 검증하지 않는다. 명시된 몰 질량이
없으면 그 원자가 들어간 분자의 몰 질량은 미상이다. 질량 0은 가상 사이트를
나타내는 데 사용할 수 있다. 지정한 물리 반지름은 데이터로 보관되며 2D 구조도
크기나 원자 간 힘에 자동 적용되지 않는다.

```cpp
#include <MolecularMap.h>
#include <array>
using namespace ii::pbr;

CustomAtomDefinition definition;
definition.id = "example:atom";     // 앱이 관리하는 안정적인 ID
definition.symbol = "X*";           // 표시용 기호
definition.name = "Example atom";
definition.protons = 140;
definition.molarMassGramsPerMol = 42.5; // 예시 값
definition.properties.push_back({"spin", 0.5, "hbar", "example"});
auto type = CustomAtomType::create(std::move(definition));
if (type) {
    auto molecule = Molecule::create({Atom::fromCustom(*type), {"H"}},
                                     {{0, 1, BondOrder::single}},
                                     "H{example:atom}");
    std::array types{*type};
    auto formula = parseMolecularFormula("H{example:atom}", types);
    // molecule과 formula의 오류를 확인한 뒤 사용한다.
}
```

`Atom::fromCustom()`은 불변인 원자 정의를 보유하며 형식 전하, 동위원소
질량수, 암묵적 수소 수를 선택적으로 받는다. 원자 하나에 실제 원소 기호와
사용자 정의 타입을 동시에 넣을 수 없다. 같은 표시 기호라도 `{example:atom}`은
실제 원소와 다른 정체성이다. 분자식의 `{ID}` 표기는 iiPBRMaterial의 확장이며
표준 화학식이나 SMILES가 아니다. ID는 1~128바이트의 ASCII 영숫자로 시작하고
이후 영숫자·`_`·`-`·`.`·`:`·`/`를 허용한다. 표시 기호는 1~32바이트의
유효한 UTF-8이며 제어 문자는 허용하지 않는다.

독립적인 `parseMolecularFormula()` 호출에는 해당 `CustomAtomType` 목록을
명시적으로 전달해야 한다. `Molecule::create()`는 원자에서 정의를 수집하므로
별도 목록이 필요 없다. `matchesFormula()`와 `customAtomTypes()`는 분자의 정의를
재사용한다. 같은 ID에 서로 다른 정의가 섞이면 분자, 이성질체 목록, 소재
카탈로그와 유기 소재 정의에서 오류를 낸다. 정의는 입력 객체를 복사해 만든
불변 스냅샷이며 여러 원자나 분자가 안전하게 공유할 수 있다. 앱은 파일에
저장하거나 다른 프로세스로 전달할 때 식별자뿐 아니라 해당 정의도 함께
보존해야 한다.

일반 분자식 입력은 최대 1,024바이트이고 사용자 원자 정의를 함께 전달하는
식은 최대 1 MiB이다. 한 식에서 사용자 원자 정의는 최대 4,096개를 받는다.

동위원소 질량수가 지정된 원자는 기존과 같이 전체 몰 질량을 비운다. 이는
사용자가 입력한 질량이 그 동위원소의 실제 질량인지 SDK가 판별하지 않기
때문이다. 표시용 기호는 2D 구조도와 SVG에 나오며 사용자 원자의 ID는
`AtomGlyph::customAtomId`와 SVG `data-custom-atom` 속성에 보존된다.
[RDKit의 사용자 표시 레이블](https://www.rdkit.org/docs/GettingStartedInPython.html)과
마찬가지로 표시와 정체성을 분리하되, 이 API는 RDKit에 의존하지 않는다.

## 결합 표현

결합은 두 원자 인덱스와 단일·이중·삼중·방향족 종류를 가진다. 단일 결합은
사용자가 지정한 실선 쐐기 또는 빗금 쐐기를 가질 수 있으며, 첫 번째 원자가
좁은 끝이고 두 번째 원자가 넓은 끝이다. 쐐기는 제공된 표현을 보존할 뿐
R/S 또는 E/Z 입체 배치를 계산하거나 검증하지 않는다. 원자 수·인덱스·중복
결합·입력 크기는 검사하지만 원자가, 방향족성, 동위원소의 실재 여부나 구조의
화학적 안정성은 판정하지 않는다. 떨어진 여러 구성 요소도 허용한다.

## 사용자 정의 결합과 물성

`CustomBondDefinition`과 `CustomBondType::create()`로 사용자가 결합의 고유 ID,
이름과 종류를 정의한다. 종류는 `covalent`, `hydrogen`, `coordinate` 또는
사용자 고유 문자열처럼 자유롭게 정할 수 있다. 실제 원소와 사용자 원자
사이에도 사용할 수 있다. `Bond::fromCustom(first, second, type)`은 불변
결합 정의를 분자 그래프의 해당 두 원자에 붙인다. 기존 단일·이중·삼중·
방향족 결합은 그대로 사용할 수 있다. 한 분자 그래프에서 같은 두 원자 사이에
중복 결합을 추가할 수 없다.

양 끝은 임의의 원자, 실제 원소 기호, 특정 사용자 원자 ID 중 하나로 제한할
수 있다. 무방향 결합은 두 끝을 바꾸어도 되고, `firstToSecond` 방향 결합은
첫 원자에서 둘째 원자로 지정한 순서를 지켜야 한다. 이는 사용자가 정의한
방향을 기록하며 전자 이동이나 배위 결합의 실제 화학성을 추론하지 않는다.
같은 분자 그래프에서 같은 사용자 결합 ID에 다른 정의를 넣으면 오류다.
이성질체들은 각각의 그래프를 보유하므로 원자 구성이 같으면 결합 방식이나
결합 정의가 달라도 별도의 후보로 보존한다.

물성은 선택적으로 직접 지정한다.

- 비정수도 가능한 결합 차수 `order`, 결합 종류와 방향.
- 해리 에너지 `dissociationEnergyJoulesPerMol`(J/mol), 출처와 선택적인 기준
  온도(K)·압력(Pa). 에너지 값을 제공할 때 출처가 필요하다.
- 평형 거리 `equilibriumLengthMetres`(m), 결합 하나에 대한 강성
  `stiffnessNewtonsPerMetre`(N/m), 참고용 파단 거리(m).
- 임의의 숫자·문자열·배열, 단위와 출처를 담는 `CustomBondProperty`.

해리 에너지는 사용자가 제공한 값이다. 해리 방식, 영점 에너지, 평균 결합
에너지 여부 등의 의미는 출처와 사용자 속성으로 명시해야 한다. 서로 다른
에너지 정의를 자동으로 변환하거나 하나로 취급하지 않는다. 예를 들어
[IUPAC의 결합 해리 에너지 정의](https://goldbook.iupac.org/terms/view/BT07006)는
평균 결합 에너지나 다른 기준의 열역학량과 구분해야 한다.

```cpp
#include <MolecularMap.h>
using namespace ii::pbr;

CustomBondDefinition definition;
definition.id = "example:link";
definition.name = "Example link";
definition.kind = "user:coordinate";
definition.direction = BondDirection::firstToSecond;
definition.first = {BondEndpointKind::customAtom, "example:A"};
definition.second = {BondEndpointKind::customAtom, "example:B"};
definition.order = 1.5;
definition.dissociationEnergyJoulesPerMol = 250000; // 예시 값
definition.energySource = "example fixture";
definition.equilibriumLengthMetres = 1.5e-10;
definition.stiffnessNewtonsPerMetre = 100;
definition.display = CustomBondDisplay::dashed;
auto type = CustomBondType::create(std::move(definition));
if (type) {
    // a와 b는 각각 example:A 및 example:B인 CustomAtomType이다.
    auto molecule = Molecule::create({Atom::fromCustom(a), Atom::fromCustom(b)},
                                     {Bond::fromCustom(0, 1, *type)});
    HarmonicBondPotential model;
    auto sample = evaluateBondPotential(*type, 1.6e-10, model);
    // molecule과 sample의 오류를 확인한 뒤 사용한다.
}
```

`HarmonicBondPotential`은 평형 거리 `r₀`와 강성 `k`가 있을 때 결합 하나의
에너지 `½k(r-r₀)²`(J)와 첫 원자에서 둘째 원자 방향의 둘째 원자에 작용하는
방사 힘 `-k(r-r₀)`(N)을 계산한다. 인장 시 힘은 음수다. 이 계수 정의는
[OpenMM의 조화 결합 항 설명](https://docs.openmm.org/8.0.0/userguide/theory/02_standard_forces.html)과
같은 형태다. J/mol 단위 해리 에너지와 결합 하나당 J 단위 퍼텐셜은 별개이며,
서로를 추론하거나 파단 거리에 도달했다고 자동으로 결합을 끊지 않는다.
`BondPotential`을 구현해 별도의 거리별 에너지·힘 모델을 제공할 수도 있다.
입력 거리와 모델 결과의 유한성을 확인하며 모델의 예외는 오류로 변환한다.
조화 모델의 에너지나 힘이 실수 범위에서 넘치거나 0이 아닌 값을 표현할 수
없을 정도로 작아지면 오류를 반환한다.
이 API는 분자 동역학 적분기나 화학 반응·결합 형성·파괴 시뮬레이터가 아니다.

사용자 결합은 구조도에서 실선·점선·두 줄·세 줄 중 사용자가 선택한 형태로
표현한다. 방향 결합에는 첫 원자에서 둘째 원자로 향하는 화살표를 추가한다.
결합 ID는 `BondStroke`/`BondWedge`와 SVG `data-custom-bond`에 보존되며 화살표
선분은 `data-bond-arrow`로 구분된다. 표시 길이는 도식 좌표이며 평형 거리와
같지 않다. 앱이 저장하거나 교환할 때는 결합 ID와 정의를 분자 그래프와 함께
보존해야 한다. 소재 카탈로그는 원소 구성만 비교하므로 결합 방식이나 에너지로
소재의 정체성을 인증하지 않는다.

예를 들어 `C2H6O`만으로 에탄올과 디메틸 에터를 구별할 수 없다. 앱은
명시적인 구조를 전달하고 물성 검색에서는 적절한 소재 ID를 선택해야 한다.
`MaterialCatalog::resolve(molecule, id)`는 원소 구성만 비교하며 구조, 상,
이성질체 또는 밀도가 일치한다고 인증하지 않는다.

## 이성질체 후보 전달과 선택

`Molecule::create(atoms, bonds, expectedFormula, isomers)`로 생성할 때
다른 `Molecule`을 전달하거나, 기존 분자에서 `withIsomers(isomers)`를 호출해
후보를 포함한 새 값을 만들 수 있다. 두 API는 명시적 원자와 암묵적 수소를
모두 포함한 **원소별 개수**가 기본 분자와 같은지만 비교한다. 불일치나 범위
초과는 오류를 반환한다. 후보의 원자·결합·형식 전하·동위원소·쐐기 정보는
각각 보존된다. 같은 그래프, 다른 전하 또는 동위원소 구성도 원소 개수가 같으면
입력할 수 있다. 사용자 정의 원자는 ID별 개수와 해당 ID의 불변 정의도 같아야
한다. 같은 ID에 다른 질량·입자 수·속성을 할당한 후보는 충돌로 거부한다.

이 비교는 요청된 입력 조건이며 화학적으로 참인 이성질체 판정이 아니다.
[IUPAC의 이성질체 정의](https://goldbook.iupac.org/terms/view/I03289)는
같은 원자 조성뿐 아니라 구성 또는 입체 배열의 차이를 요구한다. SDK는 그런
차이, 동위원소 구성의 일치 또는 실제 물질의 존재를 검증하지 않으므로 앱에서
입력의 화학적 의미를 확인해야 한다.

`withIsomers`는 기존 값을 바꾸지 않고 후보 목록을 교체한 새 값을 반환한다.
빈 목록은 새 값의 후보를 비운다. 후보 분자의 기본 원자·결합 그래프만 복사하며
그 후보에 붙은 다른 후보는 재귀적으로 포함하지 않는다. 입력 배열은 반환 이후
해제해도 된다. 기본 분자를 포함한 총 명시적 원자 1,000,000개, 총 결합
2,000,000개, 후보 4,096개까지 허용한다. 개별 분자 제한은 그대로 적용한다.

```cpp
#include <MolecularMap.h>
#include <array>
using namespace ii::pbr;

auto ethanol = Molecule::create({{"C", 0, 0, 3}, {"C", 0, 0, 2}, {"O", 0, 0, 1}},
    {{0, 1, BondOrder::single}, {1, 2, BondOrder::single}}, "C2H6O");
auto ether = Molecule::create({{"C", 0, 0, 3}, {"O"}, {"C", 0, 0, 3}},
    {{0, 1, BondOrder::single}, {1, 2, BondOrder::single}}, "C2H6O");
if (ethanol && ether) {
    std::array alternatives{*ether};
    auto family = ethanol->withIsomers(alternatives);
    if (family) {
        auto selected = family->isomer(0);       // 독립적인 분자 값
        auto diagram = buildIsomerMap(*family, 0); // 선택된 에터의 구조도
        // selected와 diagram의 오류를 확인한 뒤 사용한다.
    }
}
```

`isomers()`는 저장된 후보의 읽기 전용 원자·결합 뷰이고, `isomer(index)`는
해당 후보를 독립된 `Molecule` 값으로 반환한다. 인덱스는 기본 구조를 제외한
후보 목록의 0부터 시작한다. 기본 구조는 `buildMolecularMap`으로, 후보는
`buildIsomerMap`으로 그린다. 후자도 외부 좌표와 표시 옵션을 받는다.
`MaterialCatalog::resolveIsomer(molecule, index, materialId)`는 후보를 고른
다음 원소 구성만으로 소재를 조회한다. 같은 식의 물질은 물성이 다를 수 있으므로
앱에서 적합한 소재 ID와 조건을 명시해야 하며, 이 조회도 후보의 구조와 소재
레코드가 같은 물질인지 검증하지 않는다.

## 2D 분자 맵

`buildMolecularMap()`은 불변 `Molecule`을 받아 화면 좌표의 원자 글리프와
결합 선·쐐기를 생성한다. 기본 레이아웃은 단순 고리를 정다각형으로 배치하고
그 외 연결 성분에는 결정적인 2차원 힘 기반 배치를 사용한다. 이는 표시를
위한 근사 도식이며 실제 원자 좌표, 결합 길이, 입체 구조나 에너지를 계산하지
않는다. 복잡한 분자에서는 레이블 겹침이나 선 교차가 남을 수 있다. 자동
레이아웃이 적합하지 않으면 원자 순서에 맞는 외부 2D 좌표를 전달한다.

자동 배치는 최대 256개 원자로 제한한다. 외부 좌표를 제공하면 맵은 최대
원자 10,000개와 결합 20,000개를 허용한다. 원본 분자 객체는 최대 원자
100,000개와 결합 200,000개를 허용한다. 유한하지 않은 좌표와 서로 겹친
결합 원자, 잘못된 결합 종류 및 과도한 크기는 오류로 반환한다.

`MolecularMapSink`는 선, 쐐기, 원자 글리프를 받아 앱의 iiGUI 등 렌더러에
전달할 수 있는 인터페이스이다. 결합을 먼저, 원자를 나중에 전달한다.
렌더러가 중간에 오류를 반환하면 이전 그리기 명령의 외부 부작용은 앱이
관리한다. `molecularMapSvg()`는 같은 맵의 SVG 문자열을 메모리에서 만든다.
SVG 요소에는 원자·결합 인덱스가 있어 앱이 선택이나 하이라이트에 사용할
수 있다. SVG를 파일로 저장할 경우 앱의 iiFileProvider 연동을 사용한다.
앱에 실제 iiGUI 뷰를 마운트하거나 구조 좌표를 검색하는 기능은 포함하지 않는다.

## 사용 예

[예제 소스](../example/molecule.cpp)는 명시적인 물 분자의 원자 두 개와
산소 한 개, 두 결합을 전달해 맵과 SVG를 생성한다. 파일을 직접 생성하지
않는다. 사용 예제 빌드는 `IIPBR_BUILD_EXAMPLE=ON`으로 선택한다.

분자식 입력에는 ASCII 숫자와 Unicode 아래첨자 숫자(`H2O`, `H₂O`)를 모두
사용할 수 있다. 전하·동위원소의 식 표기 자체는 기존 파서의 지원 범위가
아니므로 분자 객체의 원자 속성으로 전달한다.

## 검증

자동 테스트는 원자 수와 입력 분자식의 일치, 이성질체 후보의 전달·독립 수명·선택,
서로 다른 구조와 물성 ID, 전하·동위원소, 잘못된 결합, 물과 벤젠의 자동·지정 좌표, 방향족·삼중·쐐기
결합, 사용자 정의 원자와 결합의 ID·끝점·에너지·힘·표시, 렌더러 오류, SVG의 실제 XML 구조와 요소 수를 검사한다. 이 테스트는
화학적 유효성 검증이나 실제 3D 렌더링의 증거가 아니다.
