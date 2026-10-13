#include "MyRunAction.hh"
#include "MyDetectorConstruction.hh"   // to read current sample material name / thickness
#include "G4Run.hh"
#include "G4SystemOfUnits.hh"
#include "G4AccumulableManager.hh"
#include "G4AnalysisManager.hh"
#include "G4RunManager.hh"
#include "G4ios.hh"
#include <cmath>
#include <ctime>
#include <fstream>
#include <atomic>
#include <mutex>
#include <cstdio>

namespace {
    std::atomic<G4double> g_primaryEnergy{0.0};

    std::mutex g_materialMutex;
    G4String g_sampleMaterial = "S1";

    void SetGlobalSampleMaterial(const G4String& name) {
        std::lock_guard<std::mutex> lock(g_materialMutex);
        g_sampleMaterial = name;
    }
    G4String GetGlobalSampleMaterial() {
        std::lock_guard<std::mutex> lock(g_materialMutex);
        return g_sampleMaterial;
    }

    std::atomic<G4double> g_sampleThickness{0.0};
    void SetGlobalSampleThickness(G4double t) {
        g_sampleThickness.store(t, std::memory_order_relaxed);
    }
    G4double GetGlobalSampleThickness() {
        return g_sampleThickness.load(std::memory_order_relaxed);
    }
}

MyRunAction::MyRunAction()
    : G4UserRunAction(),
    fEdepNaIDet(0.0), fPulseNaIDet(0.0),
    fNCompton(0.0), fNPhotoelectric(0.0), fNPairProduction(0.0),
    fNRayleigh(0.0), fNOther(0.0), fNTotal(0.0),
    fNUncollided(0.0)   // NEW: đếm hạt ra khỏi Sample mà chưa hề tương tác
{
    auto* accMan = G4AccumulableManager::Instance();
    accMan->RegisterAccumulable(fEdepNaIDet);
    accMan->RegisterAccumulable(fPulseNaIDet);
    accMan->RegisterAccumulable(fNCompton);
    accMan->RegisterAccumulable(fNPhotoelectric);
    accMan->RegisterAccumulable(fNPairProduction);
    accMan->RegisterAccumulable(fNRayleigh);
    accMan->RegisterAccumulable(fNOther);
    accMan->RegisterAccumulable(fNTotal);
    accMan->RegisterAccumulable(fNUncollided);   // NEW

    auto* analysisManager = G4AnalysisManager::Instance();
    analysisManager->SetVerboseLevel(1);
    analysisManager->SetNtupleMerging(true);
    analysisManager->SetDefaultFileType("root");

    analysisManager->CreateH1("E_dep", "Deposited Energy Spectrum in NaI(Tl)", 1000, 0.0, 3000.0, "keV");
    analysisManager->CreateH1("Pulse_Counts", "Counts per Event in NaI(Tl) Detector", 10, 0.0, 10.0, "none");

    analysisManager->CreateNtuple("Hits", "Event Data");
    analysisManager->CreateNtupleDColumn("Edep_keV");
    analysisManager->FinishNtuple();
}

MyRunAction::~MyRunAction() = default;

void MyRunAction::BeginOfRunAction(const G4Run*) {
    G4AccumulableManager::Instance()->Reset();
    auto* analysisManager = G4AnalysisManager::Instance();
    analysisManager->Reset();

    auto detector = static_cast<const MyDetectorConstruction*>(
        G4RunManager::GetRunManager()->GetUserDetectorConstruction());
    G4String matName = detector ? detector->GetSampleMaterialName() : "Unknown";
    SetGlobalSampleMaterial(matName);

    G4double thicknessMM = detector ? detector->GetSampleThickness() / mm : 0.0;
    SetGlobalSampleThickness(thicknessMM);

    auto pga = static_cast<const MyPrimaryGeneratorAction*>(
        G4RunManager::GetRunManager()->GetUserPrimaryGeneratorAction());
    if (pga) {
        g_primaryEnergy.store(pga->GetParticleEnergy(), std::memory_order_relaxed);
    }
    G4double energyKeV = g_primaryEnergy.load(std::memory_order_relaxed) / keV;

    G4String presetName = analysisManager->GetFileName();
    if (!presetName.empty()) {
        analysisManager->OpenFile();
    } else {
        char tag[128];
        std::snprintf(tag, sizeof(tag), "_t%.2fmm_E%.1fkeV_output", thicknessMM, energyKeV);
        analysisManager->OpenFile(matName + tag);
    }

    if (IsMaster()) {
        fTimer.Start();
        std::time_t t = std::time(nullptr);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
        G4cout << "\n========== Run started: " << buf << " ==========" << G4endl;
    }
}

void MyRunAction::EndOfRunAction(const G4Run* run) {
    G4AccumulableManager::Instance()->Merge();
    auto* analysisManager = G4AnalysisManager::Instance();

    if (run->GetNumberOfEvent() == 0) return;

    G4int    nEvents = run->GetNumberOfEvent();
    G4double edep = fEdepNaIDet.GetValue();
    G4double pulse = fPulseNaIDet.GetValue();
    G4double nCompt = fNCompton.GetValue();
    G4double nPhot = fNPhotoelectric.GetValue();
    G4double nPair = fNPairProduction.GetValue();
    G4double nRayl = fNRayleigh.GetValue();
    G4double nOther = fNOther.GetValue();
    G4double nTot = fNTotal.GetValue();
    G4double nUncoll = fNUncollided.GetValue();   // NEW

    if (IsMaster()) {
        // --- Human-readable output ---
        G4cout << "\n================ ANALYSIS RESULTS ================" << G4endl;
        G4cout << "Sample material  : " << GetGlobalSampleMaterial() << G4endl;
        G4cout << "Number of events: " << nEvents << G4endl;
        G4cout << "[Edep] NaI Detector = " << edep / MeV << " MeV" << G4endl;
        G4cout << "[Pulse] NaI Detector = " << pulse << G4endl;

        G4cout << "\n--- First Interaction in Sample ---" << G4endl;
        G4cout << "  Compton        = " << nCompt << G4endl;
        G4cout << "  Photoelectric  = " << nPhot << G4endl;
        G4cout << "  Pair Production = " << nPair << G4endl;
        G4cout << "  Rayleigh       = " << nRayl << G4endl;
        G4cout << "  Other          = " << nOther << G4endl;
        G4cout << "  Total          = " << nTot << G4endl;

        if (nTot > 0) {
            G4cout << "\n--- R = N_process / (N_total - N_Rayleigh) ---" << G4endl;
            G4cout << "  R_Compton        = " << nCompt / (nTot - nRayl) << G4endl;
            G4cout << "  R_Photoelectric  = " << nPhot / (nTot - nRayl) << G4endl;
            G4cout << "  R_PairProduction = " << nPair / (nTot - nRayl) << G4endl;
            G4cout << "  R_Rayleigh       = " << nRayl / (nTot - nRayl) << G4endl;
            G4cout << "  Ratio Checking   = " << (nCompt + nPhot + nPair + nRayl + nOther) / nTot << G4endl;
        }

        // ---------------------------------------------------------------
        // NEW: Uncollided flux (narrow-beam benchmark, giống T-Cross
        // ctmin=0/ctmax=0 của PHITS) -> mu đo được để so với XCOM.
        // ---------------------------------------------------------------
        G4double thicknessMM_ = GetGlobalSampleThickness();
        G4double thicknessCM = thicknessMM_ / 10.0;
        G4double fracUncoll = (nEvents > 0) ? nUncoll / (G4double)nEvents : 0.0;
        G4double muMeasured_percm = 0.0; // hệ số suy giảm tuyến tính đo được (1/cm)
        if (fracUncoll > 0.0 && thicknessCM > 0.0) {
            muMeasured_percm = -std::log(fracUncoll) / thicknessCM;
        }

        G4cout << "\n--- Uncollided Flux (narrow-beam, PHITS-style) ---" << G4endl;
        G4cout << "  N_uncollided        = " << nUncoll << G4endl;
        G4cout << "  Fraction uncollided = " << fracUncoll << G4endl;
        G4cout << "  mu_measured (1/cm)  = " << muMeasured_percm << G4endl;
        G4cout << "  (so sanh voi mu_total tra tu XCOM cho vat lieu/nang luong nay)" << G4endl;
        G4cout << "==================================================\n" << G4endl;

        fTimer.Stop();
        std::time_t t = std::time(nullptr);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
        G4double real = fTimer.GetRealElapsed();
        G4int h = (G4int)(real / 3600);
        G4int m = (G4int)((real - h * 3600) / 60);
        G4int s = (G4int)(real - h * 3600 - m * 60);
        G4cout << "========== Run ended  : " << buf << " ==========" << G4endl;
        G4cout << "========== Elapsed    : "
            << h << "h " << m << "m " << s << "s"
            << "  (real: " << std::fixed << std::setprecision(1) << real << " s)"
            << " ==========" << G4endl;

        G4double energy_keV = g_primaryEnergy.load(std::memory_order_relaxed) / keV;
        G4String matName = GetGlobalSampleMaterial();

        // --- CSV output ---
        std::string csvFile = "process_results.csv";

        std::ifstream checkFile(csvFile);
        bool writeHeader = !checkFile.good();
        checkFile.close();

        std::ofstream csv(csvFile, std::ios::app);
        if (writeHeader) {
            csv << "Material,Thickness_mm,Energy_keV,N_events,N_compton,N_photo,N_pair,N_rayleigh,N_other,N_total,"
                << "R_compton,R_photo,R_pair,R_rayleigh,Pulse_NaI,Edep_MeV,"
                << "N_uncollided,Frac_uncollided,Mu_measured_percm" << std::endl;   // NEW columns
        }

        G4double rC = (nTot > 0) ? nCompt / (nTot - nRayl) : 0;
        G4double rP = (nTot > 0) ? nPhot / (nTot - nRayl) : 0;
        G4double rPP = (nTot > 0) ? nPair / (nTot - nRayl) : 0;
        G4double rR = (nTot > 0) ? nRayl / (nTot - nRayl) : 0;

        G4double thicknessMM = GetGlobalSampleThickness();

        csv << matName << ","
            << std::fixed
            << std::setprecision(3) << thicknessMM << ","
            << std::setprecision(2)
            << energy_keV << ","
            << nEvents << ","
            << std::setprecision(0)
            << nCompt << "," << nPhot << "," << nPair << "," << nRayl << "," << nOther << "," << nTot << ","
            << std::setprecision(8)
            << rC << "," << rP << "," << rPP << "," << rR << ","
            << std::setprecision(0) << pulse << ","
            << std::setprecision(6) << edep / MeV << ","
            << std::setprecision(0) << nUncoll << ","
            << std::setprecision(8) << fracUncoll << ","
            << std::setprecision(6) << muMeasured_percm
            << std::endl;
        csv.close();
    }

    analysisManager->Write();
    analysisManager->CloseFile();
}

void MyRunAction::AddEdepNaIDet(G4double edep) {
    fEdepNaIDet += edep;
    auto* analysisManager = G4AnalysisManager::Instance();
    analysisManager->FillH1(0, edep);
    analysisManager->FillNtupleDColumn(0, edep / keV);
    analysisManager->AddNtupleRow();
}

void MyRunAction::AddCountedNaIDet() {
    fPulseNaIDet += 1.0;
    auto* analysisManager = G4AnalysisManager::Instance();
    analysisManager->FillH1(1, 1.0);
}

void MyRunAction::AddProcessCount(const G4String& processName) {
    fNTotal += 1.0;
    if (processName == "compt") fNCompton += 1.0;
    else if (processName == "phot")  fNPhotoelectric += 1.0;
    else if (processName == "conv")  fNPairProduction += 1.0;
    else if (processName == "Rayl")  fNRayleigh += 1.0;
    else                             fNOther += 1.0;
}

void MyRunAction::AddUncollided() {   // NEW
    fNUncollided += 1.0;
}
