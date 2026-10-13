// =========================================================================
// MyDetectorConstruction.cc  —  SIMPLIFIED (narrow-beam benchmark geometry)
//
// Chỉ còn World + Sample. Đã bỏ Source volume, Collimator1, Collimator2,
// NaI Detector, và sensitive detector — vì bây giờ "uncollided flux" được
// đếm trực tiếp trong MySteppingAction (kill hạt ngay khi tương tác), không
// cần một detector vật lý ở cuối chuỗi hình học nữa.
//
// Hình học khớp với các surface trong file PHITS mẫu:
//   PHITS "10  pz 0.0"          -> mặt trước Sample tại z = 0
//   PHITS "19  pz <thickness>"  -> mặt sau Sample tại z = thickness
//   PHITS "20  cz 5.0"          -> bán kính Sample = 5 cm
// =========================================================================

#include "MyDetectorConstruction.hh"

#include "G4NistManager.hh"          // Material database
#include "G4Box.hh"                  // World box
#include "G4Tubs.hh"                 // Sample cylinder
#include "G4LogicalVolume.hh"
#include "G4VPhysicalVolume.hh"
#include "G4PVPlacement.hh"
#include "G4VisAttributes.hh"
#include "G4Colour.hh"
#include "G4Region.hh"
#include "G4RegionStore.hh"
#include "G4ProductionCuts.hh"
#include "G4SystemOfUnits.hh"
#include "G4RunManager.hh"
#include <vector>
#include <utility>

namespace {
    void MakeMat(const G4String& name, G4double density,
                 const std::vector<std::pair<G4String, G4double>>& components) {
        if (G4Material::GetMaterial(name, false)) return;
        auto* NIST = G4NistManager::Instance();
        auto* mat = new G4Material(name, density, components.size());
        for (const auto& comp : components) {
            mat->AddElement(NIST->FindOrBuildElement(comp.first), comp.second);
        }
    }
}

// =========================================================================
// Geometry Dimensions
// =========================================================================
namespace GEO {
    const G4double WorldSize = 1.0 * m;    // world đủ lớn bao quanh Sample
    const G4double Sample_Radius = 5.0 * cm;   // khớp PHITS "20  cz 5.0"
}

// =========================================================================
// Constructor / Destructor
// =========================================================================
MyDetectorConstruction::MyDetectorConstruction()
    : G4VUserDetectorConstruction(),
    fMessenger(nullptr),
    fSample_Thickness(5.0 * mm),   // giá trị mặc định, ghi đè bằng macro cho từng run
    fSampleMaterial("S1")          // vật liệu mặc định, ghi đè bằng macro cho từng run
{
    fMessenger = new G4GenericMessenger(this, "/DetectorConstruction/", "Detector Construction Control");
    fMessenger->DeclareProperty("Sample_Thickness", fSample_Thickness, "Set Sample_Thickness");
    fMessenger->DeclareProperty("Sample_Material", fSampleMaterial, "Set Sample material name (e.g. S1, S2, S3...)");
    fMessenger->DeclareMethod("update", &MyDetectorConstruction::UpdateGeometry, "Update geometry after parameter changes");
}

MyDetectorConstruction::~MyDetectorConstruction() {}

// =========================================================================
// Define Materials
// =========================================================================
void MyDetectorConstruction::DefineMaterials() {
    G4NistManager* NIST = G4NistManager::Instance();

    // World: chân không
    NIST->FindOrBuildMaterial("G4_Galactic");

    // ===============================================
    // Sample Materials — theo COMPOSITION / DENSITY goc (KHONG chuan hoa)
    // ===============================================
    //
    // ZAID -> nguyen to (theo chuan ACE/ENDF ZZZAAA.xxc, "000" = tu nhien):
    //   8000=O  11000=Na  12000=Mg  13000=Al  14000=Si
    //   16000=S 19000=K   20000=Ca  26000=Fe  30000=Zn  82000=Pb
    //
    // LUU Y: tong phan tram khoi luong CUA MOT SO vat lieu KHONG bang 1.0
    // (S1: 0.9642, S2: 0.8900, S3: 0.9020, S4: 0.8811) - giu nguyen theo
    // yeu cau, khong chuan hoa lai.

    // [0]. S0: "No sample" placeholder - vacuum
    if (!G4Material::GetMaterial("S0", false)) {
        new G4Material("S0", 1, 1.008 * g / mole, 1e-25 * g / cm3,
            kStateGas, 2.73 * kelvin, 3e-18 * pascal);
    }

    // [1]. S1: Cement (CEM) — density 3.1 g/cm3
    MakeMat("S1", 3.1 * g / cm3, {
        {"O",  0.3411}, {"Ca", 0.4545}, {"Si", 0.0912}, {"Fe", 0.0259},
        {"Al", 0.0233}, {"S",  0.0108}, {"Mg", 0.0084}, {"K",  0.0048},
        {"Na", 0.0042}
    });

    // [2]. S2: Green Glass — density 2.5 g/cm3
    MakeMat("S2", 2.5 * g / cm3, {
        {"O",  0.4274}, {"Si", 0.3235}, {"Na", 0.0979}, {"Zn", 0.0145},
        {"Al", 0.0132}, {"Mg", 0.0084}, {"Fe", 0.0035}, {"S",  0.0016}
    });

    // [3]. S3: Brown Glass — density 2.52 g/cm3
    MakeMat("S3", 2.52 * g / cm3, {
        {"O",  0.4312}, {"Si", 0.3193}, {"Na", 0.0964}, {"Al", 0.0201},
        {"Zn", 0.0185}, {"Mg", 0.0103}, {"Fe", 0.0042}, {"S",  0.0020}
    });

    // [4]. S4: Transparent Glass — density 2.51 g/cm3
    MakeMat("S4", 2.51 * g / cm3, {
        {"O",  0.4236}, {"Si", 0.3216}, {"Na", 0.0972}, {"Zn", 0.0161},
        {"Al", 0.0138}, {"Mg", 0.0036}, {"Fe", 0.0028}, {"S",  0.0024}
    });

    // [5]. S7: Pure Lead (Pb) — density 11.34 g/cm3
    MakeMat("S7", 11.34 * g / cm3, {
        {"Pb", 1.0}
    });
}

// =========================================================================
// Construct Geometry
// =========================================================================
G4VPhysicalVolume* MyDetectorConstruction::Construct() {
    DefineMaterials();
    G4NistManager* NIST = G4NistManager::Instance();

    // ===============================================
    // 1. World Volume
    // ===============================================
    G4Box* solidWorld = new G4Box("World", GEO::WorldSize / 2, GEO::WorldSize / 2, GEO::WorldSize / 2);
    G4LogicalVolume* World_LV = new G4LogicalVolume(solidWorld, NIST->FindOrBuildMaterial("G4_Galactic"), "WorldLV");
    World_Phys = new G4PVPlacement(nullptr, {}, World_LV, "World", nullptr, false, 0, true);

    // ===============================================
    // 2. Sample
    // ===============================================
    // Mặt trước đặt đúng tại z = 0 (giống PHITS surface "10  pz 0.0"),
    // mặt sau tại z = fSample_Thickness (giống PHITS surface "19  pz thickness").
    G4Material* sampleMat = G4Material::GetMaterial(fSampleMaterial, false);
    if (!sampleMat) {
        G4cerr << "\n*** ERROR: Sample material \"" << fSampleMaterial
            << "\" not found (did you define it in DefineMaterials()?). "
            << "Falling back to S1.\n" << G4endl;
        sampleMat = G4Material::GetMaterial("S1");
    }

    G4Tubs* solidSample = new G4Tubs("Sample", 0, GEO::Sample_Radius, fSample_Thickness / 2, 0, 360 * deg);
    G4LogicalVolume* Sample_LV = new G4LogicalVolume(solidSample, sampleMat, "Sample_LV");
    G4double Sample_PosZ = fSample_Thickness / 2;  // mặt trước rơi đúng vào z = 0
    new G4PVPlacement(nullptr, G4ThreeVector(0, 0, Sample_PosZ), Sample_LV, "Sample", World_LV, false, 0, true);

    // ===============================================
    // VISUALIZATION
    // ===============================================
    G4VisAttributes* World_VisAtt = new G4VisAttributes(G4Colour(1, 1, 1, 0.0));
    World_VisAtt->SetVisibility(false);
    World_LV->SetVisAttributes(World_VisAtt);

    G4VisAttributes* Sample_VisAtt = new G4VisAttributes(G4Colour(0, 1, 1));
    Sample_VisAtt->SetForceSolid(true);
    Sample_LV->SetVisAttributes(Sample_VisAtt);

    // ===============================================
    // Production cuts thấp trong Sample — quan trọng để mô phỏng
    // đúng vận chuyển photon năng lượng thấp (khớp XCOM).
    // ===============================================
    G4Region* Sample_Region = G4RegionStore::GetInstance()->FindOrCreateRegion("SampleRegion");
    Sample_Region->AddRootLogicalVolume(Sample_LV);
    G4ProductionCuts* sampleCuts = new G4ProductionCuts();
    sampleCuts->SetProductionCut(1 * nm, G4ProductionCuts::GetIndex("gamma"));
    sampleCuts->SetProductionCut(1 * nm, G4ProductionCuts::GetIndex("e-"));
    sampleCuts->SetProductionCut(1 * nm, G4ProductionCuts::GetIndex("e+"));
    Sample_Region->SetProductionCuts(sampleCuts);

    return World_Phys;
}

void MyDetectorConstruction::UpdateGeometry() {
    auto runManager = G4RunManager::GetRunManager();
    runManager->ReinitializeGeometry();
    ConstructSDandField();
    G4cout << "\n================ Updated Geometry ================\n";
    G4cout << " Sample Thickness: " << fSample_Thickness / cm << " cm\n";
    G4cout << " Sample Material : " << fSampleMaterial << "\n";
}

G4String MyDetectorConstruction::GetSampleMaterialName() const {
    return fSampleMaterial;
}

G4double MyDetectorConstruction::GetSampleThickness() const {
    return fSample_Thickness;
}

// =========================================================================
// Construct Sensitive Detectors
// =========================================================================
// Không còn sensitive detector nào cả: việc đếm "uncollided flux" được làm
// trực tiếp trong MySteppingAction (kill hạt ngay khi có tương tác + đếm
// hạt vượt qua biên Sample -> World mà chưa hề tương tác).
void MyDetectorConstruction::ConstructSDandField() {
    // (để trống có chủ đích)
}