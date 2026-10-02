#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace g_Hack {
    enum class NoteExperimentScope { TidesOfFortune, AllXPNotes, Selected };
    struct NoteExperimentEntry {
        int Index = -1;
        std::string Name;
        bool Unlocked = false;
        bool PreventXPBuff = false;
        bool TidesOfFortune = false;
    };
    struct NoteExperimentBuff {
        std::string Name;
        int ObjectIndex = -1;
        float RemainingSeconds = 0;
        bool HasRemainingTime = false;
    };
    struct NoteExperimentSample {
        bool Valid = false;
        bool HasExperience = false;
        bool HasBuffs = false;
        float Experience = 0;
        std::vector<NoteExperimentBuff> Buffs;
    };
    struct NoteExperimentAttempt {
        int Index = -1;
        std::string Name;
        bool UnlockedBefore = false;
        bool UnlockedAfter = false;
        bool HasAfter = false;
    };
    struct NoteExperimentView {
        bool Ready = false;
        bool Running = false;
        bool Observing = false;
        std::uint64_t Round = 0;
        std::uint64_t CatalogGeneration = 0;
        int Sent = 0;
        int Total = 0;
        std::string Status;
        std::string LogPath;
        std::vector<NoteExperimentEntry> Entries;
        std::vector<NoteExperimentAttempt> Attempts;
        NoteExperimentSample Before, After;
    };
    void SetNoteExperimentOpen(bool Open);
    void RefreshNoteExperiment();
    void RequestNoteExperiment(NoteExperimentScope Scope, int SelectedIndex = -1,
        std::uint64_t CatalogGeneration = 0);
    void CancelNoteExperiment();
    NoteExperimentView GetNoteExperimentView();
}
