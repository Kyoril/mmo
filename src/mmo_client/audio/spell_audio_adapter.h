// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "shared/game_client/spell_visualization_service.h"
#include "audio.h"
#include <map>

namespace mmo
{
    /// \brief Adapter that implements ISpellAudioPlayer using the mmo_client IAudio interface.
    class SpellAudioAdapter : public ISpellAudioPlayer
    {
    public:
        explicit SpellAudioAdapter(IAudio& audio);

        void PlaySound(const std::string& soundFile) override;
        uint64 PlayLoopedSound(const std::string& soundFile) override;
        void StopLoopedSound(uint64 handle) override;

    private:
        IAudio& m_audio;
        uint64 m_nextHandle { 1 };
        std::map<uint64, ChannelIndex> m_loopedChannels;
    };
}
