#include "sc4/RegionDialogs.h"

#include "gz/WinListBox.h"
#include "gz/WinTextEdit.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "rz/cRZString.h"

namespace sc4 {

namespace {
constexpr std::uint32_t kRegionList = 0x1000;
constexpr std::uint32_t kNewRegionNameEdit = 0x2000;
constexpr std::uint32_t kNewRegionPlainsBtn = 0x1000;
constexpr std::uint32_t kNewRegionWaterBtn = 0x1001;
constexpr std::uint32_t kNewRegionOkBtn = 0x8a5b9959;
constexpr std::uint32_t kNewRegionCancelBtn = 0xca5b9966;
constexpr std::uint32_t kNewRegionMaxNameLen = 0x80;
constexpr std::uint32_t kUITextGroup = 0x6a231eaa;
constexpr std::uint32_t kConflictTitleId = 0x4a78b7e8;
constexpr std::uint32_t kConflictBodyId = 0x8a78b80e;
constexpr std::uint32_t kWinFlagEnabled = 0x2;

gz::cIGZWinListBox* RegionList(cIGZWin* dialog) {
    cIGZWin* child = dialog ? dialog->GetChildWindowFromIDRecursive(kRegionList) : nullptr;
    gz::cIGZWinListBox* list = nullptr;
    if (child && child->QueryInterface(gz::kGZIID_cIGZWinListBox, reinterpret_cast<void**>(&list)) && list) return list;
    return nullptr;
}

gz::cIGZWinTextEdit* NewRegionTextEdit(cIGZWin* dialog) {
    cIGZWin* child = dialog ? dialog->GetChildWindowFromIDRecursive(kNewRegionNameEdit) : nullptr;
    gz::cIGZWinTextEdit* edit = nullptr;
    if (child && child->QueryInterface(gz::kGZIID_cIGZWinTextEdit, reinterpret_cast<void**>(&edit)) && edit) return edit;
    return nullptr;
}

bool IsValidRegionName(const cRZString& name) {
    const std::uint32_t len = name.Strlen();
    if (len == 0 || len > 256) return false;
    const char* s = name.Data();
    if (!s || std::strcmp(s, ".") == 0 || std::strcmp(s, "..") == 0) return false;
    bool hasNonSpace = false;
    for (std::uint32_t i = 0; i < len; ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x20 || std::strchr("/\\:*?\"<>|", static_cast<char>(c)) != nullptr) return false;
        if (c != ' ' && c != '\t') hasNonSpace = true;
    }
    return hasNonSpace;
}
}  // namespace

// ---- cSC4NewRegionDialog ----------------------------------------------------------------------------------

bool cSC4NewRegionDialog::NameConflicts(const cRZString& name) const {
    for (const std::string& existing : host_.existingNames) {
        const cRZString rhs(existing.c_str());
        if (name.IsEqualToStringRef(rhs, false)) return true;
    }
    return false;
}

void cSC4NewRegionDialog::EnableControls() {
    cIGZWin* window = Window();
    if (!window) return;
    cIGZWin* okWin = window->GetChildWindowFromIDRecursive(kNewRegionOkBtn);
    if (!okWin) return;
    bool valid = false;
    if (cIGZWin* editWin = window->GetChildWindowFromIDRecursive(kNewRegionNameEdit)) {
        const cRZString caption(editWin->GetCaption());
        valid = IsValidRegionName(caption);
    }
    okWin->SetFlag(kWinFlagEnabled, valid);
}

void cSC4NewRegionDialog::GZOnAttach() {
    cIGZWin* window = Window();
    if (!window) return;
    if (gz::cIGZWinTextEdit* edit = NewRegionTextEdit(window)) {
        cIGZWin* editWin = edit->AsIGZWin();
        cRZString baseName = editWin ? cRZString(editWin->GetCaption()) : cRZString("New Region");
        if (baseName.Strlen() == 0) baseName = cRZString("New Region");

        cRZString candidate(baseName);
        int suffix = 0;
        while (NameConflicts(candidate)) {
            candidate.Sprintf("%s %d", baseName.ToChar(), ++suffix);
        }
        if (editWin) {
            editWin->SetCaption(candidate);
            editWin->SetNotificationTarget(window);
        }
        edit->SetMaximumTextToAllow(kNewRegionMaxNameLen);
        edit->SetFlag(gz::kTEUpDownNotify);
        edit->SetSelection(0, edit->GetTextLength());
        edit->Release();
    }

    if (gz::cIGZWinBtn* plains = GetBtnControl(kNewRegionPlainsBtn)) {
        if (cIGZWin* btnWin = plains->AsIGZWin()) btnWin->SetNotificationTarget(window);
        if (!plains->IsChecked()) plains->SetChecked(true);
    }
    if (gz::cIGZWinBtn* water = GetBtnControl(kNewRegionWaterBtn)) {
        if (cIGZWin* btnWin = water->AsIGZWin()) btnWin->SetNotificationTarget(window);
        if (water->IsChecked()) water->SetChecked(false);
    }
    EnableControls();
}

bool cSC4NewRegionDialog::DoWinProcMessage(cIGZWin* window, cGZMessage& message) {
    if (message.type == gz::kTextEditMsgChanged) {
        EnableControls();
        return true;
    }
    return cSC4DialogWinProcBase::DoWinProcMessage(window, message);
}

bool cSC4NewRegionDialog::GZOnButtonActivate(std::uint32_t id) {
    if (id == kNewRegionPlainsBtn || id == kNewRegionWaterBtn) {
        const bool waterSelected = (id == kNewRegionWaterBtn);
        if (gz::cIGZWinBtn* plains = GetBtnControl(kNewRegionPlainsBtn)) {
            plains->SetChecked(!waterSelected);
        }
        if (gz::cIGZWinBtn* water = GetBtnControl(kNewRegionWaterBtn)) {
            water->SetChecked(waterSelected);
        }
        return true;
    }
    if (id == kNewRegionOkBtn) {
        GZOnOK();
        return true;
    }
    if (id == kNewRegionCancelBtn) {
        GZOnCancel();
        return true;
    }
    return false;
}

void cSC4NewRegionDialog::GZOnCancel() {
    EndDialog(0xffffffffu);
}

void cSC4NewRegionDialog::GZOnOK() {
    cIGZWin* window = Window();
    cIGZWin* editWin = window ? window->GetChildWindowFromIDRecursive(kNewRegionNameEdit) : nullptr;
    const cRZString name = editWin ? cRZString(editWin->GetCaption()) : cRZString("");
    if (!IsValidRegionName(name)) return;

    if (NameConflicts(name)) {
        if (host_.showConflict) host_.showConflict(window, kUITextGroup, kConflictTitleId, kConflictBodyId);
        return;
    }

    std::uint32_t baseTerrainType = 0;
    if (gz::cIGZWinBtn* water = GetBtnControl(kNewRegionWaterBtn)) {
        if (water->IsChecked()) baseTerrainType = 1;
    }
    const int createdIndex = host_.createRegion ? host_.createRegion(name.ToChar(), baseTerrainType) : -1;
    EndDialog(static_cast<std::uint32_t>(createdIndex));
}

// Every region's name with its index as the item id ('#' starts a formatting code in a list item, so a leading '#' is
// doubled), the active region selected, then sorted.
void cSC4LoadRegionDialog::GZOnAttach() {
    gz::cIGZWinListBox* list = RegionList(Window());
    if (std::getenv("SC4_DEBUG")) std::fprintf(stderr, "load region: %zu regions, list %p\n", nation_.names.size(), static_cast<void*>(list));
    if (!list) return;
    for (std::size_t i = 0; i < nation_.names.size(); ++i) {
        cRZString name(nation_.names[i].c_str());
        if (name.Strlen() != 0 && name.ToChar()[0] == '#') name = cRZString(("#" + nation_.names[i]).c_str());
        list->InsertString(name, -2, static_cast<std::uint32_t>(i));
    }
    list->SetSelectionByID(static_cast<std::uint32_t>(nation_.active), true);
    list->Sort();
    list->Release();
}

void cSC4LoadRegionDialog::GZOnOK() {
    gz::cIGZWinListBox* list = RegionList(Window());
    const std::uint32_t id = list ? list->GetSelectionID() : 0xffffffffu;
    if (list) list->Release();
    EndDialog(id);
}

namespace {
class cButtonResultDialog final : public cSC4DialogWinProcBase {
public:
    cButtonResultDialog(std::uint32_t instance, std::uint32_t windowId) : cSC4DialogWinProcBase(instance, windowId, 0) {}
    bool GZOnButtonActivate(std::uint32_t id) override { EndDialog(id); return true; }
};
}  // namespace

std::uint32_t SC4RunScriptedButtonDialog(cIGZWin* parent, std::uint32_t instance, std::uint32_t windowId) {
    auto* dialog = new cButtonResultDialog(instance, windowId);
    dialog->AddRef();
    const std::uint32_t result = dialog->Display(parent);
    dialog->Release();
    return result;
}

int SC4DisplayNewRegionDialog(cIGZWin* parent, const NewRegionDialogHost& host) {
    auto* dialog = new cSC4NewRegionDialog(host);
    dialog->AddRef();
    const int result = static_cast<int>(dialog->Display(parent));
    dialog->Release();
    return result;
}

int SC4DisplayLoadRegionDialog(cIGZWin* parent, const RegionDialogNation& nation) {
    auto* dialog = new cSC4LoadRegionDialog(nation);
    dialog->AddRef();
    const int result = static_cast<int>(dialog->Display(parent));
    dialog->Release();
    return result;
}

bool SC4DisplayDeleteRegionDialog(cIGZWin* parent) {
    auto* dialog = new cSC4DeleteRegionDialog();
    dialog->AddRef();
    const std::uint32_t result = dialog->Display(parent);
    dialog->Release();
    return result != 0;
}

}  // namespace sc4
