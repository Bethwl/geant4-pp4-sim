// =========================================================================
// MySteppingAction.cc  —  NARROW-BEAM / UNCOLLIDED-FLUX MODE
//
// Nguyên tắc (giống T-Cross ctmin=0, ctmax=0 của PHITS):
//   - Nếu hạt (primary hoặc secondary) có một tương tác THẬT SỰ
//     (khác Transportation) bên trong volume "Sample" -> kill ngay lập tức,
//     kể cả mọi secondary sinh ra ở đúng bước đó.
//   - Hạt nào ra khỏi "Sample" mà chưa từng bị kill -> chắc chắn chưa hề
//     tương tác -> đếm là "uncollided".
// =========================================================================

#include "MySteppingAction.hh"
#include "MyRunAction.hh"
#include "MyEventAction.hh"

#include "G4Step.hh"
#include "G4Track.hh"
#include "G4VProcess.hh"
#include "G4VPhysicalVolume.hh"

MySteppingAction::MySteppingAction(MyRunAction* runAction, MyEventAction* eventAction)
    : fRunAction(runAction), fEventAction(eventAction)
{
}

void MySteppingAction::UserSteppingAction(const G4Step* step)
{
    auto track = step->GetTrack();
    auto preVolume = step->GetPreStepPoint()->GetTouchableHandle()->GetVolume();

    // ---------------------------------------------------------------
    // 1) Có tương tác thật trong Sample -> kill ngay (primary + secondary)
    // ---------------------------------------------------------------
    if (preVolume && preVolume->GetName() == "Sample") {
        auto process = step->GetPostStepPoint()->GetProcessDefinedStep();
        G4String procName = process ? process->GetProcessName() : "";

        if (procName != "Transportation" && procName != "CoupledTransportation") {
            // Ghi nhận loại tương tác đầu tiên của primary (tuỳ chọn, để vẫn
            // giữ được thống kê Compton/Photoelectric/Pair/Rayleigh như cũ)
            if (track->GetTrackID() == 1 && fEventAction &&
                !fEventAction->IsFirstInteractionRecorded()) {
                fRunAction->AddProcessCount(procName);
                fEventAction->SetFirstInteractionRecorded();
            }

            // Kill track hiện tại ngay lập tức
            track->SetTrackStatus(fStopAndKill);

            // Kill luôn mọi hạt thứ cấp sinh ra ở đúng bước tương tác này
            // (electron Compton, gamma tán xạ, huỳnh quang, e+/e- từ pair, ...)
            //
            // Lưu ý: trên const G4Step*, GetSecondaryInCurrentStep() trả về
            // const std::vector<const G4Track*>* (không phải G4TrackVector*
            // = std::vector<G4Track*>*), nên cần const_cast để gọi được
            // SetTrackStatus() (hàm này không phải const).
            const std::vector<const G4Track*>* secondaries = step->GetSecondaryInCurrentStep();
            for (const G4Track* sec : *secondaries) {
                const_cast<G4Track*>(sec)->SetTrackStatus(fStopAndKill);
            }
            return;
        }
    }

    // ---------------------------------------------------------------
    // 2) Hạt vượt qua khỏi Sample mà chưa hề bị kill -> uncollided
    // ---------------------------------------------------------------
    auto postVolume = step->GetPostStepPoint()->GetTouchableHandle()->GetVolume();
    if (preVolume && preVolume->GetName() == "Sample" &&
        postVolume && postVolume->GetName() != "Sample") {
        fRunAction->AddUncollided();
    }
}