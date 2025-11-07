// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spell_audio_adapter.h"

namespace mmo
{
    SpellAudioAdapter::SpellAudioAdapter(IAudio& audio)
        : m_audio(audio)
    {
    }

    void SpellAudioAdapter::PlaySound(const std::string& soundFile)
    {
        auto soundIdx = m_audio.FindSound(soundFile, SoundType::Sound2D);
        if (soundIdx == InvalidSound)
        {
            soundIdx = m_audio.CreateSound(soundFile, SoundType::Sound2D);
        }
        if (soundIdx != InvalidSound)
        {
            m_audio.PlaySound(soundIdx, nullptr);
        }
    }

    uint64 SpellAudioAdapter::PlayLoopedSound(const std::string& soundFile)
    {
        auto soundIdx = m_audio.FindSound(soundFile, SoundType::SoundLooped2D);
        if (soundIdx == InvalidSound)
        {
            soundIdx = m_audio.CreateSound(soundFile, SoundType::SoundLooped2D);
        }
        if (soundIdx != InvalidSound)
        {
            ChannelIndex ch = InvalidChannel;
            m_audio.PlaySound(soundIdx, &ch);
            if (ch != InvalidChannel)
            {
                uint64 handle = m_nextHandle++;
                m_loopedChannels[handle] = ch;
                return handle;
            }
        }
        return 0;
    }

    void SpellAudioAdapter::StopLoopedSound(uint64 handle)
    {
        auto it = m_loopedChannels.find(handle);
        if (it != m_loopedChannels.end())
        {
            ChannelIndex ch = it->second;
            m_audio.StopSound(&ch);
            m_loopedChannels.erase(it);
        }
    }
}
