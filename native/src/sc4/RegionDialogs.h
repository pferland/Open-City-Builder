// The region screen's region dialogs: create a new region, load another region (a list of the nation's regions),
// and delete the current one.
// Recovered from the Mac i386 build with symbols (cSC4LoadRegionDialog 0x006fd776.., cSC4DeleteRegionDialog 0x006fd736..,
// SC4DisplayLoadRegionDialog 0x002ebbae, SC4DisplayDeleteRegionDialog 0x002ebb32) and SimCity 4.exe x86
// (cSC4NewRegionDialog 0x00777cc0..0x00778630).
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "rz/cRZString.h"
#include "sc4/DialogWinProcs.h"

namespace sc4 {

// The nation as the dialogs read it (cISC4Nation vslots 0x18 count, 0x1c active index, 0x2c region -> vslot 0xc name).
struct RegionDialogNation {
    std::vector<std::string> names;
    int active = 0;
};

// Services for cSC4NewRegionDialog: existing region names (case-insensitive conflict check), region creation returning
// the new region index (or -1), and displaying the "Cannot create region" conflict alert.
struct NewRegionDialogHost {
    std::vector<std::string> existingNames;
    std::function<int(const std::string& name, std::uint32_t baseTerrainType)> createRegion;
    std::function<void(cIGZWin* parent, std::uint32_t group, std::uint32_t titleInstance, std::uint32_t bodyInstance)> showConflict;
};

class cSC4NewRegionDialog final : public cSC4DialogWinProcBase {
public:
    explicit cSC4NewRegionDialog(const NewRegionDialogHost& host)
        : cSC4DialogWinProcBase(0x8a5ab1cb, 0xea5ba0d1, 0x2000), host_(host) {}   // @ 0x00777ff0
    void GZOnAttach() override;                                                     // @ 0x00778630
    bool GZOnButtonActivate(std::uint32_t id) override;                             // @ 0x00777f40
    void GZOnOK() override;                                                         // @ 0x00778470
    void GZOnCancel() override;                                                     // @ 0x00777cc0
    bool DoWinProcMessage(cIGZWin* window, cGZMessage& message) override;           // @ 0x00778450

    // Helpers exposed for direct unit testing.
    bool NameConflicts(const cRZString& name) const;                                // @ 0x00777e50
    void EnableControls();                                                          // @ 0x00778390

private:
    NewRegionDialogHost host_;
};

class cSC4LoadRegionDialog final : public cSC4DialogWinProcBase {
public:
    explicit cSC4LoadRegionDialog(const RegionDialogNation& nation)
        : cSC4DialogWinProcBase(0x8a5ab1cc, 0x4a5ba0e7, 0x1000), nation_(nation) {}   // @ 0x006fd776
    void GZOnAttach() override;                                                       // @ 0x006fd940
    void GZOnOK() override;                                                           // @ 0x006fda98
    void GZOnCancel() override { EndDialog(0xffffffffu); }                            // @ 0x006fd2a4

private:
    RegionDialogNation nation_;
};

class cSC4DeleteRegionDialog final : public cSC4DialogWinProcBase {
public:
    cSC4DeleteRegionDialog() : cSC4DialogWinProcBase(0x8a5ab1ce, 0x6a5ba20c, 0) {}    // @ 0x006fd736
    void GZOnAttach() override { TranslateTokensInCaption(0x1000); }                  // @ 0x006fd924
};

// A scripted message dialog (the app's quit / save questions): shows the window of script `instance` (id `windowId`)
// modally and answers the id of the button that closed it.
std::uint32_t SC4RunScriptedButtonDialog(cIGZWin* parent, std::uint32_t instance, std::uint32_t windowId);

// The created region's index, or -1 (cancelled).
int SC4DisplayNewRegionDialog(cIGZWin* parent, const NewRegionDialogHost& host);
// The selected region's index, or -1 (cancelled).
int SC4DisplayLoadRegionDialog(cIGZWin* parent, const RegionDialogNation& nation);
// True when the player confirmed.
bool SC4DisplayDeleteRegionDialog(cIGZWin* parent);

}  // namespace sc4
